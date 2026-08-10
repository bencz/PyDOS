# Auditoria do compilador, vtables e tamanho do código

Este documento registra como investigar o pipeline do PyDOS e o estado da
auditoria iniciada com `tests/linq.py`. O objetivo continua sendo compilar
Python 3.12+ mantendo em C apenas a representação e as primitivas essenciais;
classes e algoritmos de alto nível devem ser PIR produzido a partir de Python.

## Ferramentas de diagnóstico do compilador

Sempre carregue o índice da stdlib ao investigar um programa real:

```bash
bin/pydos tests/linq.py \
    --search-path tests \
    --stdlib-idx bin/stdlib.idx \
    --dump-pir > /tmp/linq.pir

bin/pydos tests/linq.py \
    --search-path tests \
    --stdlib-idx bin/stdlib.idx \
    --dump-types > /tmp/linq-types.txt

bin/pydos tests/linq.py \
    --search-path tests \
    --stdlib-idx bin/stdlib.idx \
    --dump-escape > /tmp/linq-escape.txt

bin/pydos tests/linq.py \
    --search-path tests \
    --stdlib-idx bin/stdlib.idx \
    -v -o /tmp/linq.asm > /tmp/linq-verbose.txt
```

`--dump-pir` mostra o PIR antes das otimizações e encerra o compilador. O modo
`-v` contém AST, PIR antes e depois das otimizações e IR, além de gerar o
assembly. Para isolar passes existem:

- `--no-pir-opt`: desliga o pipeline PIR inteiro;
- `--no-sccp`, `--no-gvn`, `--no-licm`, `--no-mem2reg`;
- `--no-die` e `--no-dbe`: desligam eliminação de instruções/blocos mortos;
- `--no-specialize`: desliga especialização de valores do PIR, não a
  monomorfização das classes genéricas feita antes;
- `--no-devirt`: preserva todas as chamadas virtuais;
- `--no-scope`: desliga a inserção de escopos de arena.

Uma comparação rápida de tamanho pode ser feita gerando um assembly para cada
opção e usando `wc -c`, `wc -l` e `rg ' PROC'`.

## O que o teste LINQ revelou

O teste estava comentado porque travava no 8086. A investigação encontrou três
defeitos independentes.

### 1. Um único segmento de código 8086

O compilador colocava todas as procedures em uma única `.CODE`. O assembly do
LINQ ultrapassava o limite de 64 KiB de um segmento real-mode. O WASM chegava a
produzir um objeto, mas o objeto OMF era inválido e o WLINK falhava com
`E3011 invalid object file attribute`.

O backend 8086 agora inicia um segmento `PYCODE<n>` para cada procedure. O
modelo large já usa chamadas `FAR`, portanto essa divisão preserva a ABI e
remove o limite agregado de 64 KiB. Isto foi o motivo direto de o executável
8086 não ser construível; vtables não corrompiam o OMF.

### 2. Escopos acumulados durante a monomorfização

Cada especialização chamava a análise semântica como se fosse um novo módulo,
empilhando escopos globais. Além disso, uma atribuição comum procurava o nome
também nos pais. Assim, `result = None` dentro de um método podia encontrar um
`result` de outro escopo e produzir mensagens contraditórias como
`TypedList` não atribuível a `TypedList`.

A análise de uma especialização agora ocorre como fragmento do escopo global
já estabelecido, seus erros são propagados pelo monomorfizador e atribuições
comuns vinculam nomes no escopo corrente. Marcadores explícitos de `global` e
`nonlocal` continuam pertencendo ao escopo corrente.

### 3. Argumentos opcionais da stdlib PIR

`sorted(iterable)` tinha sido migrado para Python, mas o índice guardava apenas
o número total de parâmetros. Uma chamada separadamente compilada como
`sorted(data, key=fn, reverse=True)` empilhava valores sem conhecer seus nomes,
defaults ou ordem. Os argumentos extras chegavam a uma função PIR de um único
parâmetro e eram silenciosamente ignorados.

Para funções globais PIR, o índice agora preserva a assinatura Python no campo
que só é usado como símbolo assembler pelas funções C. O PIR builder usa essa
assinatura para:

- associar argumentos posicionais e keyword aos parâmetros corretos;
- detectar argumentos ausentes, duplicados ou inesperados;
- materializar defaults constantes (`None`, booleanos, números e strings).

`sorted` passou a ser uma implementação Python estável que aceita `key` e
`reverse`. Assinaturas opcionais de métodos PIR ainda precisam receber o mesmo
tratamento em uma evolução do formato do índice.

## Vtables: causa indireta de tamanho, não do erro de link

Vtables são necessárias no desenho atual para despacho dinâmico, dunders,
`isinstance`, herança e reflection. Removê-las por completo quebraria a meta de
compatibilidade com Python.

Elas não são, porém, um segundo namespace. O valor autoritativo de um atributo
materializado vive no `__dict__` da classe e segue o protocolo descriptor.
Chamadas dinâmicas consultam esse valor antes da vtable, inclusive para
`staticmethod`, `classmethod`, `property` e descriptors definidos pelo
programa. A evolução prevista é associar às classes uma versão de mutação e
usar a vtable como cache somente enquanto essa versão e as premissas do call
site continuarem válidas.

Objetos-classe também armazenam sua metaclasse concreta. O parser separa
`metaclass=` das bases, o construtor aplica a metaclasse explícita ou a mais
específica herdada e o lookup liga métodos compilados da metaclasse à classe
recebedora. O protocolo chama `__prepare__`, reproduz o corpo no mapping
preparado, encaminha keywords da declaração e chama `__new__` e `__init__`.
`ABCMeta` usa esse protocolo; registro virtual, cálculo de abstratos e
`__subclasshook__` permanecem em Python, enquanto o runtime implementa apenas
as pontes compactas necessárias a `type`, `isinstance` e `issubclass`.

Ainda há uma diferença de ordenação: a classe provisória recebe o corpo antes
da reprodução no mapping de `__prepare__`. Para equivalência completa, o PIR
deverá carregar explicitamente o namespace ativo durante a execução de cada
statement do corpo. Essa mudança pertence ao compilador; não deve virar uma
segunda implementação de semântica de classes no runtime C.

A ordem herdada também é única: runtime e passe de devirtualização calculam
C3. Um diamante como `D(B, C)`, com `B(A)` e `C(A)`, resolve `D, B, C, A`,
tanto para atributos materializados quanto para métodos compilados. Cada
vtable mantém um pequeno bitset que distingue slots próprios dos herdados;
assim, ao materializar o C3, slots herdados podem ser reconstruídos sem
confundir cache com definição própria.

Chamadas devirtualizadas usam `PIR_GUARDED_CALL_METHOD`, não uma chamada direta
incondicional. O runtime compara o alvo provado com o primeiro método compilado
na ordem C3 e abandona o fast path ao encontrar shadowing na instância ou um
valor materializado em qualquer classe anterior. O fallback chama o mesmo
lookup dinâmico usado por `obj.nome(...)`; portanto `Classe.metodo = substituto`
continua correto. Uma version tag de classe ainda é uma otimização futura para
tornar a validação O(1), não uma dependência semântica. `--no-devirt` continua
útil para comparar tamanho e custo do guard.

No LINQ, depois das correções, o backend 8086 gerou aproximadamente:

| Métrica | Resultado |
|---|---:|
| Procedures | 133 |
| Vtables | 7 |
| Entradas de método nas vtables | 106 |
| Chamadas dinâmicas restantes | 61 |
| Assembly | 739 KiB |
| Executável MZ | 180 KiB |

O PIR sem otimização aumentou o assembly em cerca de 2,4%, mas continuou com
as mesmas 133 procedures. Desligar devirtualização elevou as chamadas
dinâmicas de 61 para 87, sem alterar o número de funções. Isso mostra que os
passes locais funcionam, porém não existe ainda uma política eficaz de
eliminação ou compartilhamento de funções inteiras.

A monomorfização materializou as 45 funções de `Queryable[Any]`, 12 métodos
para cada especialização de `TypedList` e 11 para `TypedDict`. Como cada método
entra na vtable para permitir lookup dinâmico, todos se tornam raízes e não
podem ser removidos por uma DCE convencional.

## Próximas melhorias recomendadas

1. Definir explicitamente a política de reflection: modo totalmente dinâmico,
   modo fechado com nomes de métodos conhecidos, ou ambos selecionáveis.
2. Implementar análise de alcançabilidade de funções considerando como raízes
   apenas entry points, chamadas diretas e métodos realmente exigidos pela
   política de reflection.
3. Compartilhar corpos de métodos genéricos cuja representação continua
   `PyDosObj`; monomorfizar somente os métodos que ganham operações primitivas
   especializadas.
4. Propagar tipos concretos por chamadas e retornos para aumentar a
   devirtualização. `TY_GENERIC_INST` já é aceito pelo passe, mas muitos call
   sites ainda chegam como `Any`.
5. Evoluir o índice da stdlib para uma seção de assinaturas versionada que
   cubra funções e métodos, parâmetros keyword-only, positional-only, `*args`
   e `**kwargs`, substituindo o armazenamento compacto transitório.
6. Criar testes negativos do compilador para erros de aridade, keyword e
   escopo, além dos testes de execução DOS.

## Resultado validado

Com `linq` novamente ativo em `runtests.bat`, a suíte de integração passou em
5 de agosto de 2026:

```text
runtime nativo: 577/577
8086:          178/178
386:           178/178
```

Os comandos de compilação, montagem, linkedição e execução estão documentados
em [linux-build-and-test.md](linux-build-and-test.md).
