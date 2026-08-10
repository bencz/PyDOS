# Fronteira entre a runtime C e a biblioteca Python

O objetivo arquitetural do PyDOS é manter a runtime C pequena. C fornece a
representação dos objetos e operações que não podem ser expressas sem acessar
essa representação. Algoritmos visíveis ao usuário pertencem à biblioteca
Python e são compilados para PIR junto com o programa.

## Regra de dependência

```text
programa Python
      |
      v
stdlib Python (algoritmos e classes de alto nível)
      |
      v
runtime C (representação e primitivas)
      |
      v
DOS / Open Watcom
```

A runtime C não deve depender de uma implementação paralela dos algoritmos da
stdlib. Uma função deve permanecer em C quando precisa manipular diretamente
ponteiros far, layouts de `PyDosObj`, buffers, estado de exceção, GC, chamadas
DOS ou outra operação fundamental que o próprio Python compilado usará como
bloco de construção.

## Modelo de memória do 8086

O alvo real mode usa o modelo `large` do Open Watcom (`wcc -ml`). Essa é a
base adequada: código e dados podem ocupar múltiplos segmentos, enquanto os
modelos small, medium e compact impõem um limite agregado de 64 KiB justamente
ao lado que um programa Python ultrapassa primeiro. O modelo huge não é uma
melhoria automática; ele normaliza ponteiros e acrescenta custo sem permitir
que um único objeto ultrapasse de forma útil o limite de um segmento.

Referências a `PyDosObj`, buffers no far heap e estruturas que atravessam
segmentos precisam continuar sendo ponteiros `far`. Estruturas auxiliares
comprovadamente confinadas ao DGROUP ou à stack podem usar ponteiros `near`.
Essa distinção deve ser explícita: retirar `far` indiscriminadamente causa
aliasing por truncamento de segmento; usá-lo em toda estrutura interna aumenta
tamanho, tráfego de memória e pressão sobre os 640 KiB convencionais.

O allocator agora prefixa cada bloco com um cabeçalho alinhado de oito bytes,
contendo tamanho e classe (`object`, `metadata`, `buffer` ou `general`). Oito
bytes não são desperdício opcional: um experimento com cabeçalho de seis bytes
desalinhou o payload retornado pelo far heap do Open Watcom e corrompeu valores
somente no 8086. Uma asserção de compilação protege essa propriedade.

As classes permitem estatísticas exatas de uso, pico e falhas nos dois alvos,
além de preparar pools separados sem alterar a ABI dos ponteiros retornados.
Objetos, blocos do GC, vtables, buffers de texto/bytes e storage de containers,
MROs e tabelas de internamento usam classes explícitas. Os wrappers antigos
`pydos_far_alloc` e `pydos_far_realloc` permanecem apenas como API compatível
da classe `general`; o runtime novo não deve introduzir chamadas a eles.

O scope allocator é um ledger de referências, não uma marca global no objeto.
Cada ticket representa uma referência owned e é liberado em ordem reversa.
Antes de `RETURN`, o compilador retém o resultado e depois libera os tickets
locais. Essa ordem é obrigatória no 8086: retornar um alias local depois de
liberá-lo parecia funcionar no 386, mas o free-list real-mode reutilizava o
objeto antes que o chamador o observasse.

Para aplicações grandes, a próxima evolução é criar pools segmentados por
classe sobre esse cabeçalho, começando por blocos de `PyDosObj` e pequenos
buffers. A decisão dos tamanhos dos buckets deve usar os picos medidos, não
constantes copiadas de ambientes modernos. EMS/XMS pode servir como backing store
para documentos e assets frios, mas não para objetos acessados a cada opcode.
Overlays são apropriados para módulos de código raramente usados. No real mode,
o cache de objetos e a stack também devem ter budgets menores que no 386; eles
dividem memória convencional com o executável e o far heap.

## Refcount e coleta de ciclos

O ciclo de vida dos objetos combina duas estratégias. `PYDOS_DECREF` libera
imediatamente grafos sem ciclos. Um coletor complementar usa trial deletion
para localizar componentes que continuam com refcount positivo apenas por
referências internas.

Todo tipo capaz de possuir uma referência forte a outro `PyDosObj` usa este
layout:

```text
[ GCHeader ][ PyDosObj ]
             ^
             ponteiro usado pelo restante da runtime
```

`GCHeader` contém os links da lista circular e `gc_refs`, usado somente durante
a coleta. `pydos_obj_alloc_type()` é a única entrada pública para alocar um
objeto. Tipos escalares usam o free-list de `PyDosObj`; tipos rastreáveis são
encaminhados a `pydos_gc_alloc_type()`, que cria o bloco combinado e o liga à
lista antes de qualquer alocação aninhada do construtor. O alocador sem tipo é
privado, portanto um novo construtor não pode criar acidentalmente um container
sem cabeçalho.

Os tipos rastreados são list, dict, tuple, set, instance, function, cell,
generator, coroutine, exception, class, ExceptionGroup e frozenset. Um único
walker descreve todas as arestas fortes desses layouts e é reutilizado para
marcação e trial deletion. Manter duas descrições independentes desse grafo é
proibido, pois uma diferença entre elas pode preservar leaks ou liberar objetos
alcançáveis.

Uma coleta completa executa:

1. limpeza das marcas transitórias em todos os objetos rastreados;
2. marcação das raízes explícitas;
3. propagação iterativa da marcação, sem recursão na stack C;
4. cópia de refcount para `gc_refs` e subtração de cada aresta interna;
5. marcação dos componentes com referências externas restantes;
6. varredura em três passagens dos objetos não marcados.

Na varredura, todos os mortos recebem temporariamente `REFCOUNT_MAX`, depois
seus buffers e referências internas são liberados e somente então os blocos
completos são removidos da lista e devolvidos ao far heap. Isso impede uma
cascata de DECREF de liberar duas vezes membros do mesmo ciclo.

O caminho normal de refcount também conhece o layout combinado. Quando um
objeto rastreado chega a zero, `pydos_obj_free()` primeiro o remove da lista,
libera seu conteúdo e devolve o endereço do `GCHeader`, nunca o ponteiro que
aponta para o meio do bloco. Objetos rastreados não entram no free-list de
escalares.

A propagação de marcas usa uma varredura repetida da lista com o bit transitório
`OBJ_FLAG_GC_SCANNED`. Essa escolha evita alocação durante uma tentativa de
recuperação após falta de memória e evita uma chamada C por nível de nesting no
8086. O custo de pior caso é quadrático em grafos profundos; trocar por uma work
list exigirá uma estratégia de overflow que continue funcionando quando a
alocação que disparou o GC já falhou.

`stdlib/gc.py` mantém a política pública em Python. `collect(generation=2)`
aceita as gerações 0, 1 e 2 por compatibilidade de fonte, mas executa sempre a
única coleta completa disponível. `is_tracked(obj)` consulta a primitiva de
rastreamento. A runtime C expõe somente as duas pontes necessárias.

Limites deliberados atuais:

- uma única geração;
- coleta automática a cada 500 alocações rastreadas;
- no máximo 64 slots de raízes explícitas;
- ausência de weak references e finalizadores do GC;
- pausa proporcional ao número total de objetos rastreados.

Os testes de regressão incluem ciclos próprios, ciclos mútuos, list/dict,
cell/list, mutação de uma raiz entre coletas, destruição normal por refcount,
pressão no limiar automático e um ciclo profundo de 192 containers. O teste
DOS `gc312` valida a superfície Python e a coleta real de `a.append(a)` em
8086 e 386.

## Modelo de exceções

O runtime usa propagação explícita de erro. A implementação antiga baseada em
`setjmp`/`longjmp`, sua pilha global de frames e o pool fixo de handlers foram
removidos: saltos não locais impediam que o compilador representasse cleanup,
arenas, `finally` e `__exit__` no CFG.

O modelo atual segue estas regras:

- uma exceção pendente permanece no estado do runtime;
- funções que retornam objetos usam `NULL` como sentinel;
- primitivas com retorno C usam status e parâmetro de saída, ou um flag de
  exceção consultável;
- um `PIR_CHECK_EXCEPTION` após operações `may_raise` desvia para um landing
  pad explícito;
- os landing pads executam DECREF, saída de arena, `finally` e `__exit__` antes
  de encaminhar a exceção;
- somente o limite externo do programa imprime uma exceção não tratada e
  encerra o processo.

O check não segue toda operação. A classificação `may_raise` o omite para
constantes, loads comprovadamente seguros e primitivas infalíveis. `FOR_ITER`
possui tratamento próprio para distinguir exaustão normal de uma exceção
levantada por `__next__`.

## O que pertence à runtime C

- alocação, refcount, coleta de ciclos e representação de `PyDosObj`;
- criação e acesso aos valores primitivos (`int`, `float`, `bool`, `str`);
- armazenamento de strings, indexação, slicing, concatenação, comparação e
  hash;
- armazenamento de listas, tuplas, dicionários e conjuntos, incluindo
  operações primitivas de leitura, escrita, inserção e remoção;
- iteradores fundamentais e despacho polimórfico dessas primitivas;
- estado de exceção pendente, vtables, interface DOS e I/O.

No I/O, “interface DOS” significa somente operações que exigem interrupções,
handles ou buffers nativos. O runtime fornece `_pydos_file_open/read/write`
e `_pydos_file_close`; ele não implementa o objeto público retornado por
`open()`.

O despacho polimórfico também é uma primitiva. Por exemplo,
`pydos_obj_getitem` precisa funcionar para strings mesmo quando o compilador
não preservou uma dica estática de tipo. O mesmo vale para `pydos_obj_slice`.

## O que pertence à stdlib Python

Cada classe mantém seus próprios métodos de alto nível:

- `stdlib/builtins/str.py`: transformação de caixa, trim, predicados, busca,
  contagem, substituição, `join` e `zfill`;
- `stdlib/builtins/list.py`: `extend`, `remove`, `index`, `count` e `copy`;
- `stdlib/builtins/dict.py`: views materializadas, `pop`, `update`,
  `setdefault`, `copy` e `popitem`;
- `stdlib/builtins/set.py`: operações de conjuntos, cópia, relações e update;
- `stdlib/builtins/tuple.py`: `count` e `index`;
- `stdlib/builtins/pyfncs.py`: somente funções globais declaradas em
  `funcs.py`, como `any`, `all`, `sum`, `sorted` e `enumerate`.

I/O e TUI seguem a mesma regra:

- `stdlib/pydos/io/base.py`: ciclo de vida comum de streams;
- `stdlib/pydos/io/text.py`: buffering, leitura por linha e iteração;
- `stdlib/pydos/io/files.py`: `open`, validação e helpers de arquivo;
- `stdlib/pydos/io/tui/canvas.py`, `screen.py`, `keyboard.py` e `clock.py`:
  responsabilidades independentes do terminal;
- `stdlib/pydos/io/tui/widgets/`: widget base, controles, factories e event
  loop em módulos separados.

Os `__init__.py` de `pydos.io`, `pydos.io.tui` e `widgets` são apenas fachadas
públicas. Os metadados das pontes C também ficam separados em
`stdlib/builtins/dosio.py` e `dostui.py`; não são algoritmos públicos.

`open()` é ligado automaticamente a partir da implementação Python quando o
símbolo aparece no programa. O objeto `TextFile` implementa context manager e
iteração; erros das primitivas DOS chegam como `OSError`, sem serem convertidos
silenciosamente em EOF ou escrita de zero bytes.

`@internal_implementation("simbolo_c_")` marca deliberadamente uma primitiva
C. Um método sem esse decorador precisa possuir um corpo Python real; não deve
ser apenas uma declaração com `...`.

## Como os métodos Python entram no executável

Ao construir `bin/stdlib.idx`, `compiler/stdbld.cpp` executa duas tarefas:

1. lê assinaturas e decoradores para montar o registro da stdlib;
2. compila `pyfncs.py` e os arquivos das classes que possuem métodos Python.

Métodos são serializados com o mesmo nome usado pelo compilador para classes,
por exemplo `str__upper` e `list__count`. Durante a compilação do programa, o
worklist de `compiler/pirmrg.cpp` copia para o módulo somente as funções PIR
alcançáveis. Portanto, um programa que não chama `str.upper()` não incorpora o
corpo desse método.

## Estado transitório e próximo backlog

Alguns símbolos C antigos ainda existem como fallback para chamadas de método
cujo tipo só é conhecido em runtime. Eles não são mais selecionados pelas
chamadas estaticamente tipadas dos métodos já migrados. Removê-los exige que o
despacho dinâmico consiga apontar para métodos PIR da stdlib.

Os principais algoritmos ainda candidatos à migração são:

- `str.split`, `str.rsplit`, `str.splitlines`, padding com argumento opcional e
  formatação;
- `list.reverse` e `list.sort`;
- operações algorítmicas de `frozenset`;
- outros helpers C que apenas percorrem coleções sem acessar representação
  interna de forma essencial.

Funções globais PIR já preservam uma assinatura compacta no índice e suportam
keyword arguments e defaults constantes; `sorted(key=..., reverse=...)` é o
primeiro caso validado. Métodos com argumentos opcionais ainda dependem de uma
seção de assinaturas versionada que cubra também as classes. Até isso existir,
esses métodos permanecem no caminho C para não introduzir parâmetros não
inicializados.

## Validação

Reconstrua o compilador e o índice:

```bash
CCACHE_DISABLE=1 make -f Makefile.mac compiler
```

Execute toda a integração DOS nos dois alvos:

```bash
WATCOM=/caminho/para/openwatcom tests/run_dos_linux.sh all
```

Baseline validada em 6 de agosto de 2026:

```text
runtime nativo: 583/583
8086:          179/179
386:           179/179
```

O procedimento completo de compilação, linkedição e execução no Linux está em
[linux-build-and-test.md](linux-build-and-test.md).
