# Roadmap de compatibilidade com Python 3.12+

Este documento é a matriz de trabalho para aproximar o PyDOS de Python 3.12.
A referência normativa é a documentação oficial de
[builtins](https://docs.python.org/3.12/library/functions.html),
[tipos embutidos](https://docs.python.org/3.12/library/stdtypes.html),
[exceções](https://docs.python.org/3.12/library/exceptions.html) e o
[índice de módulos](https://docs.python.org/3.12/py-modindex.html).

Python 3.12 é a baseline. Recursos adicionados em 3.13 ou posteriores devem
ser rastreados depois, sem alterar silenciosamente a semântica da baseline.

## Regra arquitetural

- C contém representação, alocação, GC, buffers, aritmética primitiva,
  despacho, estado de exceção pendente e acesso ao DOS.
- Python contém algoritmos, validações que podem ser expressas na linguagem,
  classes e módulos de alto nível.
- Uma API Python não deve possuir duas implementações concorrentes. Fallbacks
  C antigos devem desaparecer quando o despacho PIR equivalente estiver
  disponível.
- Toda API nova precisa de teste de chamada estaticamente tipada, chamada
  dinâmica quando aplicável, teste nativo da primitiva C e execução 8086/386.

## Bloqueadores fundamentais

Estes itens têm prioridade sobre a expansão indiscriminada da stdlib porque
afetam muitas APIs ao mesmo tempo.

| Área | Estado atual | Trabalho necessário |
|---|---|---|
| Inteiros | `long` de 32 bits | representação arbitrária, conversões, hash, comparação e aritmética de big integers |
| Texto | armazenamento orientado a bytes | representação Unicode por code point, UTF-8/16/32, classificação Unicode e codecs |
| Objetos de tipo | classes de usuário e tipos primitivos possuem identidade `type`; `object`, C3 MRO, `__mro__`, herança múltipla, `__class__`, `__dict__`, métodos ligados e o protocolo de construção de metaclasses explícitas/herdadas funcionam | ordem completa de execução do corpo em namespaces preparados, mutação de bases e reflection restante |
| Descritores | protocolo `__get__`/`__set__`/`__delete__`, `property`, `classmethod` e `staticmethod`; decorators e marcadores abstratos são preservados | `__set_name__` e auditoria dos casos de erro/reflection restantes |
| Protocolos | muitos fallbacks retornam `0`/`None` | `TypeError`, `NotImplemented`, operações refletidas e coerção corretas |
| Imports | `from modulo import nome` liga fontes recursivamente, incluindo busca implícita em `stdlib` | objetos de módulo, `import modulo`, packages, `__import__`, cache e imports relativos |
| Iteradores | núcleo existe, APIs limitadas | iteradores próprios para `map`, `filter`, `zip`, `enumerate`, `range` e sentinel iterator |
| Assinaturas | suporte parcial | positional-only, keyword-only, `*args`, `**kwargs` e mensagens de erro compatíveis |
| Limites DOS | ponteiros far e segmentos de 64 KiB | políticas explícitas de `MemoryError`/`OverflowError`, sem corrupção silenciosa |

### Stress test integrado do núcleo

O teste de aceitação avançado deve combinar, no mesmo programa e sem
imports, os recursos que interagem no CFG: `try/except/else/finally`, retorno
e exceção dentro de `finally`, `break`/`continue`, recursão, chaining com
`raise ... from`, context managers aninhados, decorators, lambdas, `*args` e
`**kwargs`, geradores com `send`/`throw`/`close`, `yield from` e
`ExceptionGroup`/`except*`. Ele é um teste de conformidade, não um substituto
dos casos unitários: quando uma construção ainda não compilar, a lacuna deve
ser registrada e implementada, nunca removida silenciosamente do caso.

## Funções built-in

### Presentes, mas ainda precisam de auditoria semântica

`abs`, `all`, `any`, `bin`, `bool`, `bytearray`, `bytes`, `callable`, `chr`,
`complex`, `delattr`, `dict`, `divmod`, `enumerate`, `filter`, `float`, `frozenset`, `getattr`,
`hasattr`, `hash`, `hex`, `id`, `input`, `int`, `isinstance`, `issubclass`,
`iter`, `len`, `list`, `map`, `max`, `min`, `next`, `oct`, `open`, `ord`,
`pow`, `print`, `range`, `repr`, `reversed`, `set`, `setattr`, `sorted`,
`str`, `sum`, `super`, `tuple`, `type`, `vars(obj)`, `zip` e `object`.

Pontos já conhecidos:

- os objetos `type` embutidos ainda não possuem toda a hierarquia baseada em
  `object`; metaclasses de usuário têm identidade, binding, herança,
  `__prepare__`, namespaces customizados, `__new__`, `__init__` e keywords de
  classe. Ainda falta executar cada statement do corpo diretamente no mapping
  preparado; hoje o compilador materializa o corpo e o runtime o reproduz no
  namespace antes de chamar `__new__`;
- `iter` não possui a forma `iter(callable, sentinel)`;
- `min`/`max`, `map` e `zip` aceitam menos formas que Python 3.12;
- `print`, `open` e `sorted` têm assinaturas/semântica parciais;
- `open` já retorna um stream Python com `with`, iteração, `read`, `readline`,
  `write` e fechamento automático, mas ainda suporta somente os modos `r`,
  `w` e `r+`; binary I/O, seek/tell, codecs reais e política universal de
  newlines permanecem pendentes;
- `vars()` sem argumento depende da materialização do frame/local namespace;
- `chr`/`ord` ainda estão limitados pela representação textual atual.

### Ausentes

`aiter`, `anext`, `ascii`, `breakpoint`, `compile`,
`dir`, `eval`, `exec`, `format`, `globals`, `help`, `locals`,
`memoryview`, `round`, `slice`
e `__import__`.

### Decorators e classes abstratas

Decorators de função, classe e método são avaliados de cima para baixo e
aplicados de baixo para cima. Métodos decorados são materializados no
`__dict__` da classe, sombreiam sua entrada compilada na vtable e funcionam
como método ligado, via `getattr`, ou como chamada não ligada pela classe.

`property`, `classmethod` e `staticmethod` são classes escritas em Python e
ligadas ao programa somente quando usadas, para não consumir memória
convencional em executáveis 8086 que não precisam delas. O runtime C expõe
somente lookup de descriptors e o primitivo de ligação de função a objeto.
Redefinições sucessivas do mesmo nome no corpo da classe recebem símbolos
internos únicos, permitindo o padrão `@property`, `@nome.setter` e
`@nome.deleter` sem alterar a chave Python visível no `__dict__`.

### Regra única de lookup, mutação e vtables

O `__dict__` da classe e o protocolo descriptor são a fonte de verdade
semântica. A ordem observável é:

1. data descriptor encontrado na classe/MRO;
2. `__dict__` da instância;
3. non-data descriptor ou valor comum da classe/MRO;
4. `__getattr__`, quando aplicável.

Uma chamada `obj.nome(...)` deve produzir o mesmo lookup que obter
`obj.nome` e chamar o resultado. Entradas materializadas no `__dict__` sempre
sombreiam métodos compilados. A vtable é apenas um fast path compacto para um
método compilado quando não existe valor dinâmico capaz de alterar a
semântica; ela não constitui um segundo namespace Python. Mutações de classe,
metaclasses e `setattr` deverão invalidar ou impedir esse fast path nos casos
afetados. Até a invalidação versionada existir, os caminhos dinâmicos consultam
primeiro o dicionário da classe.

A devirtualização usa `PIR_GUARDED_CALL_METHOD`. O call site carrega o alvo
direto provado por C3, mas o runtime confirma que não surgiu atributo na
instância, valor materializado no `__dict__` da classe ou outro método próprio
antes dele. Se a prova deixou de valer, a mesma instrução cai no lookup
dinâmico. Assim `Classe.metodo = substituto`, `del Classe.metodo`, descriptors
e shadowing da instância preservam a semântica Python. Uma version tag poderá
reduzir esse guard a O(1) no futuro, sem mudar o contrato do opcode.

Classes armazenam uma linearização C3 materializada, e o mesmo algoritmo é
usado pelo lookup do runtime, por `issubclass`/`isinstance`, pela herança de
slots da vtable e pela devirtualização do compilador. No alvo DOS há limites
explícitos de oito bases diretas e 32 entradas no MRO; ultrapassá-los deve
produzir `TypeError`, nunca truncamento silencioso. Alteração dinâmica de
`__bases__` ainda não é suportada.

`stdlib/abc.py` fornece `ABCMeta`, `ABC`, `abstractmethod`,
`update_abstractmethods`, `get_cache_token` e os decorators abstratos legados.
`ABCMeta` é um objeto-classe associado concretamente à classe criada:
`type(ABC) is ABCMeta`, a sintaxe `metaclass=ABCMeta` funciona e subclasses
herdam a metaclasse com detecção de conflito. Métodos da metaclasse são ligados
à classe, de modo que `MinhaABC.register(Subclasse)` e o uso de `register` como
decorator preservam o primeiro argumento correto.

Cada ABC mantém registro virtual próprio; `issubclass` e `isinstance`
reconhecem classes registradas, descendentes delas e a propagação de um
registro feito numa ABC derivada para suas ABCs-base. Ciclos são rejeitados,
o token de cache muda ao registrar e `_abc_registry_clear()` limpa o registro.
`property`, `classmethod` e `staticmethod` propagam `__isabstractmethod__`, e
uma implementação concreta de qualquer desses descriptors remove corretamente
o nome de `__abstractmethods__`.

O protocolo geral de metaclasses aceita `__prepare__`, namespaces de classe
customizados, `__new__`, `__init__`, keywords adicionais e herança da
metaclasse. `issubclass` e `isinstance` também consultam um
`__subclasshook__` customizado antes da relação nominal e do registro virtual.
O teste diferencial `meta312` cobre metaclasse explícita e herdada, mapping
preparado e keywords; `abc312` cobre o hook estrutural nos alvos 8086 e 386.

A limitação observável restante é a ordem de efeitos do corpo: por enquanto o
corpo é compilado para a classe provisória e depois reproduzido no mapping de
`__prepare__`. Leituras e escritas do namespace chegam corretamente a
`__new__`, mas efeitos colaterais do mapping durante cada statement ainda não
ocorrem intercalados exatamente como no CPython. Corrigir isso requer um
contexto de namespace de classe no PIR, não mais lógica de alto nível no
runtime C.

`breakpoint`, `compile`, `eval`, `exec` e `help` dependem de decisões sobre
compilação/execução dinâmica e ambiente interativo. Não devem ser simulados
com resultados falsos.

## Tipos built-in

| Tipo | Implementado | Backlog Python 3.12 |
|---|---|---|
| `str` | maioria dos métodos de busca, caixa, trim, split, join e encode | `format`, `format_map`, `maketrans`, `translate`; Unicode e casos de borda de todos os métodos |
| `bytes` | representação, indexação, slicing, concat/repeat, `decode`, `hex` | toda a família textual binária: caixa, trim, busca, split, join, replace, translate, predicates, `fromhex` e `maketrans` |
| `bytearray` | primitivas mutáveis, cópia, busca básica, reverse, decode/hex e operações de sequência | mesmos métodos binários de `bytes`, preservando retorno mutável, mais validação completa de slices |
| `int` | aritmética de 32 bits, bits, ratio simples, propriedades | `to_bytes`, `from_bytes` e big integers; auditoria de overflow, shifts e conversões |
| `float` | aritmética, propriedades, `is_integer`, `conjugate` | `as_integer_ratio`, `hex`, `fromhex`, NaN/infinito e arredondamento correto |
| `complex` | armazenamento, aritmética, propriedades e `conjugate` | parsing de strings, validação do construtor, NaN/infinito e erros de tipo |
| `bool` | valor e coerção | herança/comportamento como subtipo de `int` e objeto de tipo correto |
| `list` | todos os nomes públicos principais | auditoria de slices mutáveis, iteradores, sort, erros e comparações |
| `tuple` | construtor com preservação de identidade, `count`, `index`, hash e protocolos básicos de sequência | auditoria de concat/repeat/slice, subclasses e erros completos |
| `dict` | operações principais | `fromkeys`, views reais/dinâmicas, união `|`/`|=`, ordem e erros completos |
| `set`/`frozenset` | nomes públicos principais | argumentos variádicos, operadores/comparações, coerções e retorno correto por subtipo |
| `range` | construção validada, tamanho, indexação, containment, atributos, slicing, igualdade, `count` e `index` | hash e casos-limite condicionados a big integers |
| `memoryview` | ausente | buffer protocol, slicing multidimensional e toda a API pública |
| `slice` | nó sintático, sem objeto completo | construtor e `indices()` |
| iteradores | geradores e iteradores internos | `__iter__`, `__next__`, comprimento restante e objetos lazy compatíveis |

Também precisam ser auditados os protocolos comuns: `repr`, `str`, `hash`,
truthiness, comparação rica, containment, chamadas, context managers,
descritores, reflexão e operações in-place/refletidas.

O lowering de context managers já trata múltiplos itens como `with` aninhados,
chama managers anteriores quando um `__enter__` posterior falha, respeita a
supressão de exceções e executa cleanup em `return`, `break` e `continue`.
Essa pilha de unwind é compartilhada com `try/finally`, evitando regras
paralelas de controle de fluxo.

## Exceções

Já existem as bases mais usadas e parte da hierarquia. Faltam, no mínimo:

- `BaseExceptionGroup` e construtor público de `GeneratorExit`;
- `ReferenceError`, `SystemError`, `TabError` e `UnicodeTranslateError`;
- subclasses de `OSError`: `BlockingIOError`, `ChildProcessError`,
  `ConnectionError`, `BrokenPipeError`, `ConnectionAbortedError`,
  `ConnectionRefusedError`, `ConnectionResetError`, `FileExistsError`,
  `InterruptedError`, `IsADirectoryError`, `NotADirectoryError` e
  `ProcessLookupError`;
- categorias de warning: `Warning`, `BytesWarning`, `DeprecationWarning`,
  `EncodingWarning`, `FutureWarning`, `ImportWarning`,
  `PendingDeprecationWarning`, `ResourceWarning`, `RuntimeWarning`,
  `SyntaxWarning`, `UnicodeWarning` e `UserWarning`;
- atributos próprios das exceções (`name`, `path`, `errno`, `filename`,
  offsets de `SyntaxError`, dados de codecs etc.), aliases e mensagens.

Não basta registrar nomes: herança, matching de `except`, argumentos,
atributos, `str()` e encadeamento (`__cause__`, `__context__`, traceback)
precisam ser compatíveis.

## Biblioteca padrão

Ainda não há uma árvore de módulos Python equivalente à stdlib completa;
`abc` é o primeiro módulo de alto nível ligado diretamente de fonte. O
inventário módulo por módulo, incluindo itens incompatíveis ou condicionais no
MS-DOS, está em [python-312-module-matrix.md](python-312-module-matrix.md).
A ordem de implementação deve respeitar dependências.

1. Núcleo de plataforma: `sys`, `builtins`, `types`, `os`, `errno`, `io`,
   `time`, `math`, `struct`, `codecs` e `abc`.
2. Blocos reutilizáveis em Python: `operator`, `functools`, `itertools`,
   `collections`, `contextlib`, `enum`, `dataclasses`, `copy` e `heapq`.
3. Texto e dados: `string`, `re`, `textwrap`, `json`, `csv`, `configparser`,
   `base64`, `binascii`, `hashlib`, `decimal` e `fractions`.
4. Sistema e caminhos: `os.path`, `pathlib`, `stat`, `glob`, `fnmatch`,
   `shutil`, `tempfile` e `argparse`.
5. Testes/diagnóstico: `traceback`, `warnings`, `logging`, `unittest`,
   `pprint`, `inspect` e `dis` onde o modelo compilado permitir.
6. Recursos condicionais: `socket`, `ssl`, `subprocess`, `threading`,
   `multiprocessing`, `sqlite3`, `ctypes`, `tkinter` e demais módulos que
   dependem de serviços inexistentes ou opcionais no DOS. Esses itens não
   fazem parte da promessa-base de compatibilidade 8086.

O índice oficial de módulos Python 3.12 é a lista completa de referência.
Cada módulo recebe na matriz uma decisão inicial explícita: implementável,
dependente de primitiva DOS, opcional externo, dependente de rede,
incompatível com 8086, específico de outro sistema, ferramenta do runtime
CPython ou removido/depreciado.

## Ordem de execução

### Marco 1 — correção do núcleo

- trocar retornos silenciosos por exceções corretas;
- finalizar protocolos de operadores e `NotImplemented`;
- completar builtins simples e `range`;
- completar `bytes`/`bytearray`, `int`, `float`, `dict` e construtores;
- criar testes diferenciais executáveis também no CPython 3.12.

### Marco 2 — modelo de objetos

- objetos `type`, descritores, MRO, class/static methods e properties;
- reflection restante (`dir`, `vars`, métodos ligados e objetos de classe);
- objetos `slice`, views de dict e iteradores próprios.

### Marco 3 — Unicode e big integers

- representação e algoritmos fundamentais;
- codecs e normalização necessários à stdlib;
- remoção das limitações semânticas de 8/32 bits.

### Marco 4 — módulos essenciais

- infraestrutura de import/package;
- módulos dos grupos 1 a 4;
- suíte de compatibilidade por módulo.

### Marco 5 — recursos avançados e específicos de plataforma

- async completo, introspecção, warnings/tracebacks ricos;
- decisões explícitas para rede, processos, threads e extensões.

## Critério de conclusão por item

Um item só é considerado concluído quando assinatura, retorno, efeitos
laterais, exceções, protocolos dinâmicos e casos de borda relevantes passam
nos testes nativos, 8086 e 386. Existência do nome, isoladamente, não conta
como compatibilidade.
