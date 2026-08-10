# Handoff técnico — exceções, ownership e modelo de memória

Este documento registra o estado do PyDOS ao encerrar a sessão de 5 de agosto
de 2026. Ele deve ser lido antes de continuar mudanças no compilador, runtime,
stdlib, modelo de memória ou stress tests.

## Resultado consolidado

A baseline atual está funcional nos dois alvos DOS:

```text
runtime nativo: 577/577
8086:          178/178
386:           178/178
```

A execução completa incluiu `linq`, `matrix`, `editmodel`, `arndep`, ABC,
metaclasses, descriptors, generators, context managers, I/O, TUI, exceções e
os testes Python 3.12 já registrados em `runtests.bat`.

O `README.md` não foi alterado, conforme solicitado.

## Decisões arquiteturais que não devem ser revertidas sem evidência

### Fronteira C/Python

- C contém somente representação, alocação, refcount/GC, buffers, operações
  primitivas, despacho essencial, estado de exceção e acesso ao DOS.
- Classes, validações e algoritmos de alto nível pertencem à stdlib Python.
- Uma API pública não deve manter implementações C e Python concorrentes.
- Vtables são cache/fast path; `__dict__`, MRO e descriptors são a fonte de
  verdade semântica.

### 8086

- Continuar usando Open Watcom `large model` (`wcc -0 -ml`).
- Objetos e buffers que atravessam segmentos usam ponteiros `far`.
- Não trocar globalmente para `huge`: o custo de normalização não remove o
  limite útil de um bloco individual e piora o hot path.
- Dados comprovadamente confinados à stack/DGROUP podem ser `near`, mas isso
  deve ser decidido por estrutura, nunca por substituição global.
- Cada procedure gerada permanece em seu próprio segmento `PYCODE<n>`; isso é
  o que permite ao LINQ ultrapassar 64 KiB de código agregado no 8086.

### 386

- O runtime usa `wcc386 -3s -mf -dPYDOS_32BIT`.
- O executável final usa CauseWay e `clib3s`.
- O fato de um teste passar no 386 não prova ownership correto: o 8086 e seu
  free-list expuseram aliases liberados prematuramente que ficavam ocultos no
  heap flat.

## Exceções: estado atual

A implementação com `setjmp`/`longjmp` foi removida. Não devem ser
reintroduzidos `ExcFrame`, frame stack, pool de handlers, `try_enter`,
`pydos_exc_push`, `pydos_exc_pop` ou saltos não locais.

O novo contrato é:

1. `pydos_exc_raise()` ou `pydos_exc_raise_obj()` grava a exceção pendente;
2. uma primitiva que falha retorna `NULL` ou status de erro;
3. operações PIR classificadas como `may_raise` recebem
   `PIR_CHECK_EXCEPTION`;
4. o check desvia para um landing pad explícito no CFG;
5. o landing pad executa arena exit, DECREF, `finally` e `__exit__`;
6. somente o limite externo chama o panic de exceção não tratada.

Operações/opcodes adicionados:

- `PIR_CHECK_EXCEPTION` / `IR_CHECK_EXCEPTION`;
- `PIR_CLEAR_EXCEPTION` / `IR_CLEAR_EXCEPTION`;
- `PIR_PANIC_EXCEPTION` / `IR_PANIC_EXCEPTION`.

`PIR_SETUP_TRY` e `PIR_POP_TRY` podem continuar aparecendo como metadados de
estrutura, mas os backends não criam frames C para eles. A transferência real
é feita pelos checks e edges explícitos.

`FOR_ITER` distingue dois casos de retorno `NULL`: exaustão normal sem exceção
pendente e falha de `__next__` com exceção pendente. `tests/dn_iter.py` possui
um iterador cujo `__next__` levanta `ValueError` para proteger esse contrato.

Bare `raise` dentro de um handler usa a exceção salva pelo próprio handler;
ele não depende de manter o estado global pendente depois do clear.

Comando de auditoria que deve continuar sem resultados no código:

```bash
rg -n "setjmp|longjmp|ExcFrame|pydos_exc_(try_enter|alloc_frame|push|pop)" \
    runtime compiler rttests tests samples
```

Menções históricas na documentação são intencionais.

## Ownership e arenas

A arena não é um allocator independente e não marca objetos. Ela é um ledger
de referências owned:

- cada `SCOPE_TRACK` registra uma referência que deverá ser liberada;
- `SCOPE_EXIT` libera tickets em ordem inversa;
- objetos retidos por containers, atributos ou globais sobrevivem por meio do
  refcount normal;
- `OBJ_FLAG_ARENA` foi removido;
- `PYDOS_DECREF` ignora somente objetos imortais.

Antes de um `RETURN`, o passe de arena insere `INCREF` no valor retornado e só
depois executa `SCOPE_EXIT`. O `IR_RETURN.extra` informa ao backend que o
resultado já foi normalizado para referência owned.

Os parâmetros de uma função compilada são borrowed. Se um retorno é alias de
um parâmetro e não foi normalizado pelo passe de arena, os backends 8086 e 386
comparam o resultado com os parâmetros e fazem `INCREF`. Isso cobre casos como
`__enter__` retornando `self`.

Não voltar a usar free forçado em arena exit. Isso quebra aliases guardados em
containers e atributos.

Testes centrais:

- `rttests/t_arn.c`;
- `tests/arndep.py`;
- `tests/withret.py`;
- `tests/editmodel.py`;
- `tests/flowcln.py`;
- `tests/tryret.py`.

Para isolar uma suspeita de arena, compile temporariamente com `--no-scope`.
Isso é somente diagnóstico, não solução permanente.

Limites atuais da ledger:

- profundidade rastreável: 16 scopes;
- tickets simultâneos: 512;
- scopes além da profundidade mantêm um contador de overflow para que seus
  exits não removam o scope pai;
- tickets acima do limite deixam de ser rastreados e podem causar leak, mas
  não devem causar free prematuro ou corrupção.

`pydos_arena_scope_track()` e `pydos_arena_scope_track_ref()` ainda possuem o
mesmo armazenamento. A distinção de API registra a origem semântica do ticket
e poderá permitir políticas diferentes depois; não eliminar essa informação
no PIR/IR sem uma auditoria de ownership.

## Allocator e modelo de memória

`runtime/pdos_mem.c` prefixa cada alocação com:

```text
unsigned long  size
unsigned short magic
unsigned char  kind
unsigned char  reserved
```

No Open Watcom DOS esse cabeçalho ocupa oito bytes. O payload precisa começar
alinhado. Uma tentativa de usar tamanho de 16 bits reduziu o cabeçalho para
seis bytes e fez `tests/editmodel.py` produzir valores incorretos somente no
8086. Com `--no-scope` o sintoma desaparecia por mudança no padrão do heap,
mas a causa real era o payload desalinhado. O cabeçalho de oito bytes restaurou
o resultado e existe agora uma asserção de compilação para proteger o layout.

Classes atuais de memória:

| Classe | Conteúdo |
|---|---|
| `PYDOS_MEM_OBJECT` | `PyDosObj` e blocos combinados do GC |
| `PYDOS_MEM_METADATA` | arrays de list/dict/frozenset, vtables, MRO, bases, intern table e arrays de ExceptionGroup |
| `PYDOS_MEM_BUFFER` | dados de str/bytes/bytearray, buffers de formatação, join e I/O |
| `PYDOS_MEM_GENERAL` | compatibilidade e alocações ainda não especializadas |

O runtime já usa classes explícitas. `pydos_far_alloc` e
`pydos_far_realloc` permanecem como wrappers compatíveis da classe `general`,
mas novas chamadas internas não devem ser adicionadas. Verificação:

```bash
rg -n "pydos_(far_alloc|far_realloc)" runtime --glob '*.[ch]'
```

O resultado esperado contém somente declarações/wrappers em `pdos_mem.h/c`.

Estatísticas disponíveis:

- alocações e bytes cumulativos;
- alocações e bytes atuais;
- pico global;
- bytes atuais e pico por classe;
- número de falhas de alocação.

O limite real-mode por bloco continua menor que 64 KiB. Uma solicitação que
inclui cabeçalho e ultrapassa `0xFFF0` falha de forma explícita.

## Mapa da implementação alterada

### Compilador

- `compiler/pir.h`, `pir.cpp`, `pirprt.cpp`: novos opcodes e impressão;
- `compiler/pirbld.cpp/.h`: pilha de destinos de exceção, landing pads,
  handlers, bare raise e fluxo de try/with/finally;
- `compiler/pirutil.cpp/.h`: classificação de operações `may_raise`;
- `compiler/pirlwr.cpp`: lowering dos novos opcodes e metadados de retorno;
- `compiler/ir.h`, `ir.cpp`: representação IR;
- `compiler/codegen.cpp/.h`: dispatch e externs;
- `compiler/cg8086.cpp/.h`, `cg386.cpp/.h`: checks, panic, `FOR_ITER`, retorno
  owned e remoção dos frames C;
- `compiler/piropt.cpp`: inserção de arena, retenção antes de return e análise
  de transferências persistentes;
- `compiler/piresc.cpp`: escape analysis usada pelo passe;
- `compiler/main.cpp`: flags de diagnóstico já existentes e pipeline;
- `compiler/modpath.cpp/.h`, `modscan.cpp`, `stdbld.cpp`, `stdgen.cpp`: busca e
  materialização de módulos/stdlib trabalhadas na mesma sequência.

### Runtime

- `runtime/pdos_exc.c/.h`: estado pendente, fetch, clear e panic;
- `runtime/pdos_gen.c`: propagação explícita em iteradores/generators;
- `runtime/pdos_arn.c/.h`: ledger de referências;
- `runtime/pdos_mem.c/.h`: cabeçalho alinhado, classes e estatísticas;
- `runtime/pdos_obj.c/.h`: ownership, containers, descriptors, classes e
  classificação das alocações;
- `runtime/pdos_gc.c`, `pdos_vtb.c`, `pdos_lst.c`, `pdos_dic.c`, `pdos_itn.c`,
  `pdos_fzs.c`, `pdos_exg.c`: metadata/object allocation;
- `runtime/pdos_str.c`, `pdos_byt.c`, `pdos_bya.c`, `pdos_sjn.c`, `pdos_io.c`,
  `pdos_blt.c`: buffer allocation;
- `runtime/pdos_tui.c/.h`: primitivas DOS/BIOS da TUI; widgets permanecem em
  Python.

### Testes principais adicionados ou ampliados

- runtime nativa: `rttests/t_mem.c`, `t_exc.c`, `t_gen.c`, `t_asn.c`,
  `t_arn.c`;
- exceções/cleanup: `tests/dn_iter.py`, `flowcln.py`, `tryret.py`, `withret.py`;
- memória/ownership: `tests/arndep.py`, `editmodel.py`;
- I/O/TUI: `tests/open312.py`, `file312.py`, `tui312.py`, `tuiwid.py`,
  `catmodel.py`;
- os testes têm seus respectivos `.exp` e, quando precisam importar samples,
  arquivos `.flags`.

## Próxima evolução do modelo de memória

Não implementar pools, EMS/XMS ou overlays sem primeiro medir programas reais.
A ordem recomendada é:

1. Expor/registrar a telemetria do allocator no harness de diagnóstico sem
   alterar a saída normal dos programas.
2. Medir pelo menos `hello`, `matrix`, `linq`, EDIT, Alley Cat e o stress test
   Python 3.12 nos dois alvos.
3. Registrar pico por classe, quantidade/tamanho das alocações e maior bloco
   livre antes/depois da carga.
4. Projetar um pool segmentado de `PyDosObj`, considerando que já existe um
   free-list de 64 objetos no 8086 e 256 no 386.
5. Projetar buckets somente para pequenos buffers/metadata que os dados
   mostrarem frequentes. Não copiar tamanhos de CPython ou sistemas modernos.
6. Reexecutar a suíte completa e comparar memória convencional, fragmentação,
   tamanho do executável e tempo.
7. Somente depois considerar EMS/XMS para documentos/assets frios.
8. Considerar overlays para módulos de código raramente executados.

Objetos acessados a cada opcode, vtables, frames ativos, stack e strings
quentes devem permanecer em memória convencional. EMS/XMS exige cópia ou
mapeamento de página e não serve como heap transparente para o hot path.

## Riscos e limitações ainda abertos

- A telemetria existe na API C, mas ainda não é coletada automaticamente pelo
  runner DOS nem associada a cada sample.
- Ainda não existe um objeto de `MemoryError` de emergência pré-alocado para o
  caso em que criar a própria exceção também falha.
- Falhas de alocação precisam de auditoria completa para garantir que todas as
  primitivas estabeleçam `MemoryError`, em vez de apenas retornar `NULL`.
- O estado de exceção é global, aceitável no runtime DOS atual de uma thread;
  qualquer evolução real de threads exigirá estado por execução/thread.
- Tracebacks ricos, `__context__`, `__cause__` e `__suppress_context__` ainda
  precisam ser validados pelo stress case, mesmo que o parser aceite
  `raise ... from`.
- A semântica completa de generators com handlers atravessando `yield`,
  `throw`, `close`, `GeneratorExit` e `StopIteration.value` ainda precisa do
  stress case e de testes mínimos específicos no código compilado.
- A execução do corpo de classe ainda é materializada primeiro na classe
  provisória e depois reproduzida no mapping de `__prepare__`; os efeitos por
  statement no namespace customizado não estão completamente intercalados.
- Unicode real e inteiros arbitrários permanecem bloqueadores grandes para
  compatibilidade Python 3.12, independentemente do modelo de memória.
- O build nativo ainda mostra warnings antigos de variável/typedef não usados;
  eles não causaram failures, mas devem ser limpos em uma auditoria separada.

## Stress case Python 3.12 pendente

Foi proposto um stress case grande, sem imports, que combina:

- decorators e registro de testes;
- `*args` e `**kwargs`;
- `try/except/else/finally`;
- retorno ou nova exceção dentro de `finally`;
- exception chaining com `raise ... from`;
- `break`/`continue` com cleanup;
- recursão com exceções;
- context managers simples e aninhados;
- lambdas, closures e comprehensions;
- generators, `yield from`, `send`, `throw`, `close` e `GeneratorExit`;
- `StopIteration.value`;
- `ExceptionGroup`, grupos aninhados e `except*`;
- mutação de estado em `finally`.

Esse caso ainda não foi materializado em `tests/stress312.py` e não está em
`runtests.bat`. Na próxima sessão, recuperar o fonte integral da conversa (ou
solicitá-lo novamente ao usuário), salvá-lo sem simplificações e gerar a saída
normativa com CPython 3.12. Não usar o Python 3.14 do host como única referência.

O fluxo correto é:

1. salvar o caso integral;
2. executar no CPython 3.12 e criar `tests/stress312.exp` com LF;
3. tentar compilar sem remover construções não suportadas;
4. registrar cada falha como lacuna de parser, PIR, backend ou runtime;
5. criar testes mínimos para cada lacuna;
6. só registrar o stress case em `runtests.bat` quando ele compilar e executar;
7. mantê-lo como gate de conformidade a partir daí.

## Comandos de build e validação

O Open Watcom local é detectado em:

```text
toolchains/openwatcom/distribution
```

Compilador e índice da stdlib:

```bash
CCACHE_DISABLE=1 make -f Makefile.mac compiler
```

Runtime nativa:

```bash
CCACHE_DISABLE=1 make -f Makefile.mac test
```

Suíte DOS completa:

```bash
tests/run_dos_linux.sh all
```

Suíte focal para exceções, ownership e memória:

```bash
tests/run_dos_linux.sh all \
    hello arndep editmodel dn_iter finally matrix ft_exc exc_adv \
    withret open312 file312
```

O runner configura `WATCOM`, `INCLUDE` e `PATH`, compila a runtime 8086 com
`wcc -0 -ml`, a runtime 386 com `wcc386 -3s -mf`, monta com WASM, linka com
WLINK e executa no DOSEMU2 usando `-3`.

Para executar um sample TUI interativamente, o usuário deve usar, por exemplo:

```bash
dosemu -quiet -3 -K build/samples/8086/edit -E edit.exe
```

Testes automatizados validam os modelos e a saída de console, mas não
substituem a inspeção interativa completa da TUI.

## Diagnóstico do compilador

Opções importantes de `bin/pydos`:

```text
--dump-pir
--dump-types
--dump-escape
-v
--no-pir-opt
--no-sccp
--no-gvn
--no-licm
--no-specialize
--no-scope
--no-mem2reg
--no-die
--no-devirt
--no-dbe
--no-func-dedup
```

`--dump-pir` mostra PIR antes da otimização. `-v` inclui AST, PIR antes/depois,
IR e gera assembly. Sempre carregar `--stdlib-idx bin/stdlib.idx` e os search
paths necessários ao reproduzir um programa real.

## Cuidados com o worktree

O worktree contém muitas alterações relacionadas desta sequência de trabalho,
incluindo compiler, runtime, stdlib, tests e samples. Não usar `git reset`,
`git checkout --` ou limpeza destrutiva para tentar obter uma árvore limpa.

A pasta `docs/` está atualmente excluída localmente por `.git/info/exclude`:

```text
/docs/
```

Por isso estas atualizações não aparecem em `git status`. Os arquivos existem
no workspace, mas, se o usuário quiser versioná-los, será necessário remover a
regra local ou adicionar os documentos conscientemente com `git add -f`.
Não alterar essa política automaticamente.

Antes de qualquer commit:

```bash
git diff --check
git status --short
```

## Arquivos de referência

- `docs/runtime-architecture.md`: fronteira C/Python, memória, ownership e
  exceções;
- `docs/linux-build-and-test.md`: toolchain, compilação, linkedição e DOSEMU2;
- `docs/compiler-debugging-and-modernization.md`: PIR, otimizadores, vtables,
  tamanho de código e LINQ;
- `docs/python-312-compatibility-roadmap.md`: lacunas e ordem de compatibilidade;
- `docs/python-312-module-matrix.md`: decisão por módulo da stdlib;
- este documento: estado exato e sequência para retomar a sessão.
