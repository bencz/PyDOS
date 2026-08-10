# Compilar, linkar e testar executáveis DOS no Linux

Este documento registra o procedimento usado para validar o PyDOS no Linux.
Ele mantém o compilador no host e usa o DOS somente para executar o programa
final:

```text
Python -> bin/pydos (Linux) -> WASM assembly -> Open Watcom (Linux)
       -> executável DOS -> emulador
```

Isso evita executar o compilador dentro de uma máquina DOS e torna possível
capturar `stdout`, códigos de saída e diferenças de teste diretamente no shell
do Linux.

## Estado da validação

Em 5 de agosto de 2026, no Fedora 44 x86-64, foram validados:

| Etapa | 8086 | 386/CauseWay |
|---|---:|---:|
| Compilador PyDOS nativo | OK | OK |
| Compilação da runtime com Open Watcom | OK | OK |
| Montagem e linkedição do `hello.py` | OK | OK |
| Execução com emu2 | OK | Não suportada |
| Execução com DOSEMU2 `-3` | OK | OK |
| Suíte de integração | 178/178 | 178/178 |

O executável 8086 produzido foi um MZ DOS de 104 KiB e imprimiu
`Hello, DOS!` no emu2. O executável 386 produzido foi um LE/CauseWay de
189 KiB. Ao tentar executá-lo no emu2, o próprio CauseWay informou
`CauseWay error 02: 386 or better required`, confirmando que o emu2 deve ser
usado somente para o alvo 8086.

O DOSEMU2 aceitou a opção `-3`, confirmou `CPU set to 386` e executou os dois
formatos. Tanto o MZ 8086 quanto o LE/CauseWay imprimiram `Hello, DOS!` e
terminaram com status zero. No modo `-quiet`, as saídas dos dois executáveis
foram comparadas com `tests/hello.exp` sem nenhuma diferença.

O teste `linq`, antes desativado por travar no 8086, também foi reativado. O
backend agora divide cada procedure em seu próprio segmento `PYCODE<n>`,
evitando que programas grandes ultrapassem o limite de 64 KiB de um único
segmento real-mode. A análise detalhada está em
[compiler-debugging-and-modernization.md](compiler-debugging-and-modernization.md).

Os testes nativos da runtime também foram executados no host: 577/577
passaram. O alvo `rttest` linka explicitamente `libm`, necessária pelas
primitivas de números complexos.

Inicialmente, o shell do DOSEMU2 falhou procurando por
`/usr/i386-pc-dj64/lib/crt0.elf`. A instalação de `dj64dev-dj64` corrigiu o
ambiente; a falha acontecia antes de qualquer executável do PyDOS ser carregado.

## Dependências

- GCC/G++ e GNU Make para construir o compilador nativo;
- [Open Watcom v2](https://github.com/open-watcom/open-watcom-v2) para gerar
  objetos OMF, bibliotecas e executáveis DOS;
- [emu2](https://github.com/dmsc/emu2) para os testes rápidos do alvo 8086;
- [DOSEMU2](https://dosemu2.github.io/dosemu2/) para o alvo 386/CauseWay;
- QEMU com FreeDOS, futuramente, como teste de maior fidelidade.

O projeto mantém a distribuição portátil estável do Open Watcom v2 em uma
pasta local ignorada pelo Git. Para baixá-la, extrair e criar aliases por
plataforma:

```bash
scripts/setup-openwatcom.sh
```

Também é possível baixar manualmente `ow_portable_v2_stable.zip`, colocá-lo em
`toolchains/openwatcom/downloads/` e executar o script. O layout e as
plataformas contidas no pacote estão descritos em
[`toolchains/README.md`](../toolchains/README.md).

O runner configura esse ambiente automaticamente. Para executar ferramentas
manualmente, use:

```bash
export WATCOM="$PWD/toolchains/openwatcom/distribution"
export PATH="$PWD/toolchains/openwatcom/hosts/linux/bin:$WATCOM/binw:$PATH"
export INCLUDE="$WATCOM/h"

wasm -?
wlink
```

Os executáveis Linux do Open Watcom são ELF de 32 bits. Eles funcionaram no
host Fedora usado nesta validação. Em ambientes isolados por seccomp, como um
sandbox restrito, pode ser necessário permitir sua execução fora do sandbox.

### Construir o emu2

```bash
git clone --depth 1 https://github.com/dmsc/emu2.git /caminho/para/emu2
make -C /caminho/para/emu2
export PYDOS_EMU2=/caminho/para/emu2/emu2
```

Se o compilador estiver sendo chamado por `ccache` em um ambiente que não
permite escrever no cache, use `CCACHE_DISABLE=1 make`.

### Instalar o DOSEMU2 no Fedora

O próprio projeto publica um repositório COPR para Fedora:

```bash
sudo dnf copr enable stsp/dosemu2
sudo dnf install dosemu2
sudo dnf --refresh install dj64dev-dj64
```

Antes de testar o PyDOS, confirme que o DOS chega ao shell:

```bash
dosemu -dumb -3
```

## Construir o compilador nativo

Na raiz do repositório:

```bash
make -f Makefile.mac compiler
```

Apesar do nome histórico, esse Makefile também constrói no Linux. Os artefatos
necessários para as próximas etapas são:

- `bin/pydos`;
- `bin/stdlib.idx`.

Para executar também os testes nativos da runtime no host:

```bash
CCACHE_DISABLE=1 make -f Makefile.mac test
```

## Construir as runtimes DOS no host Linux

O `Makefile` principal ainda contém comandos e separadores de caminho do DOS.
Por enquanto, as bibliotecas podem ser construídas com as ferramentas Linux do
Open Watcom usando os comandos abaixo.

Comece criando uma área de build ignorada pelo Git:

```bash
export PYDOS_DOS_BUILD=build/dos-linux
mkdir -p "$PYDOS_DOS_BUILD/rt16" "$PYDOS_DOS_BUILD/rt32"
```

### Runtime 8086

```bash
for source in runtime/pdos_*.c; do
    name=${source##*/}
    wcc -0 -ml -ox -zq -fpc \
        -fo="$PYDOS_DOS_BUILD/rt16/${name%.c}.obj" \
        "$source"
done

objects=()
for object in "$PYDOS_DOS_BUILD"/rt16/*.obj; do
    objects+=("+$object")
done

wlib -q -n "$PYDOS_DOS_BUILD/PYDOSRT.LIB" "${objects[@]}"
```

As opções importantes são `-0` para limitar o código ao 8086 e `-ml` para o
modelo de memória large usado pela runtime.

### Runtime 386

```bash
for source in runtime/pdos_*.c; do
    name=${source##*/}
    wcc386 -3s -mf -ox -zq -fpc -dPYDOS_32BIT \
        -fo="$PYDOS_DOS_BUILD/rt32/${name%.c}.obj" \
        "$source"
done

objects=()
for object in "$PYDOS_DOS_BUILD"/rt32/*.obj; do
    objects+=("+$object")
done

wlib -q -n "$PYDOS_DOS_BUILD/PDOS32RT.LIB" "${objects[@]}"
```

Aqui `-3s` seleciona o 386 com convenção de chamada por pilha, `-mf` seleciona
o modelo flat e `PYDOS_32BIT` ativa a representação 32-bit da runtime.

## Compilar e linkar um programa 8086

Gere o assembly com o compilador nativo:

```bash
bin/pydos tests/hello.py \
    -o "$PYDOS_DOS_BUILD/hello.asm" \
    --stdlib-idx bin/stdlib.idx
```

Monte o objeto OMF:

```bash
wasm -0 -ml -d0 -zq \
    -fo="$PYDOS_DOS_BUILD/hello.obj" \
    "$PYDOS_DOS_BUILD/hello.asm"
```

Linke o MZ DOS com a runtime do PyDOS e as bibliotecas 16-bit do Watcom:

```bash
wlink \
    system dos \
    option stack=32768 \
    option dosseg \
    option eliminate \
    name "$PYDOS_DOS_BUILD/hello.exe" \
    file "$PYDOS_DOS_BUILD/hello.obj" \
    library "$PYDOS_DOS_BUILD/PYDOSRT.LIB" \
    library clibl \
    library emu87
```

Confirme o formato produzido:

```bash
file "$PYDOS_DOS_BUILD/hello.exe"
```

O resultado esperado começa com `MS-DOS executable, MZ for MS-DOS`.

## Compilar e linkar um programa 386/CauseWay

```bash
bin/pydos tests/hello.py \
    -o "$PYDOS_DOS_BUILD/hello386.asm" \
    -t 386 \
    --stdlib-idx bin/stdlib.idx

wasm -3 -mf -d0 -zq \
    -fo="$PYDOS_DOS_BUILD/hello386.obj" \
    "$PYDOS_DOS_BUILD/hello386.asm"

wlink \
    system causeway \
    option stack=65536 \
    option dosseg \
    option eliminate \
    name "$PYDOS_DOS_BUILD/hello386.exe" \
    file "$PYDOS_DOS_BUILD/hello386.obj" \
    library "$PYDOS_DOS_BUILD/PDOS32RT.LIB" \
    library clib3s
```

Confirme o formato:

```bash
file "$PYDOS_DOS_BUILD/hello386.exe"
```

O resultado validado foi `MS-DOS executable, LE for unknown OS 0x1`. O stub
CauseWay é incorporado pelo `wlink`; não é necessário copiar um extender ao
lado do programa.

## Executar e comparar um teste 8086 com emu2

O emu2 escreve diretamente no `stdout` do Linux e propaga o código de saída
informado por `INT 21h/AH=4Ch`. Isso permite um teste sem janela e sem interação:

```bash
set -o pipefail
"$PYDOS_EMU2" "$PYDOS_DOS_BUILD/hello.exe" \
    | sed 's/\r$//' \
    > "$PYDOS_DOS_BUILD/hello.out"

diff -u tests/hello.exp "$PYDOS_DOS_BUILD/hello.out"
```

O `sed` normaliza o CRLF emitido pelo programa DOS para o LF dos arquivos
`.exp`. Um `diff` sem saída e com status zero significa que o teste passou.

Também é possível observar a saída diretamente:

```bash
"$PYDOS_EMU2" "$PYDOS_DOS_BUILD/hello.exe"
```

Saída validada:

```text
Hello, DOS!
```

## Executar e comparar com DOSEMU2

O modo `-quiet` conecta a saída padrão do programa DOS ao shell e remove os
banners de boot. `-3` força uma CPU 80386, `-K` monta a pasta indicada e `-E`
executa o comando sem uma sessão interativa:

```bash
dosemu -quiet -3 \
    -K "$PWD/$PYDOS_DOS_BUILD" \
    -E hello.exe

dosemu -quiet -3 \
    -K "$PWD/$PYDOS_DOS_BUILD" \
    -E hello386.exe
```

Saída validada para ambos:

```text
Hello, DOS!
```

Para comparar automaticamente o alvo 386 com o resultado esperado (troque por
`hello.exe` para testar o alvo 8086):

```bash
set -o pipefail
dosemu -quiet -3 \
    -K "$PWD/$PYDOS_DOS_BUILD" \
    -E hello386.exe \
    | sed 's/\r$//' \
    | diff -u tests/hello.exp -
```

Esse comando foi executado com status zero e sem saída do `diff`. A opção `-3`
também foi confirmada pelo diagnóstico `CPU set to 386` no modo `-dumb`.

### Aplicações TUI interativas

Não use `-quiet` para os samples TUI. O wrapper do DOSEMU2 traduz essa opção
para o modo terminal `dumb`, destinado à captura linear de `stdout`. Esse modo
não implementa corretamente posicionamento do cursor, cores e redesenho de
tela; uma janela pode aparecer apenas como caracteres sobrepostos na primeira
coluna.

Use o modo terminal real `-t`, em um terminal com pelo menos 80 colunas e 25
linhas:

```bash
dosemu -t -3 -K build/samples/8086/tui_demo -E tui_demo.exe
dosemu -t -3 -K build/samples/8086/alley_cat -E alley_cat.exe
dosemu -t -3 -K build/samples/8086/edit -E edit.exe

dosemu -t -3 -K build/samples/386/tui_demo -E tui_demo.exe
dosemu -t -3 -K build/samples/386/alley_cat -E alley_cat.exe
dosemu -t -3 -K build/samples/386/edit -E edit.exe
```

O modo `-quiet` continua correto para a suíte automatizada, pois esses testes
comparam somente a saída padrão e não exercitam uma interface de tela.

Se o boot falhar com `unable to open /usr/i386-pc-dj64/lib/crt0.elf`, instale o
runtime DJ64 e repita o teste:

```bash
sudo dnf --refresh install dj64dev-dj64
```

## Runner automatizado

O runner reconstrói o compilador e as runtimes, monta, liga, executa no
DOSEMU2, normaliza CRLF e compara cada saída com seu arquivo `.exp`:

```bash
tests/run_dos_linux.sh 8086 hello str_mtd
tests/run_dos_linux.sh 386 hello str_mtd
tests/run_dos_linux.sh all
```

`WATCOM` não precisa ser informado quando a distribuição local está em
`toolchains/openwatcom/distribution`. Para evitar que um executável DOS com
loop infinito trave a suíte, cada teste tem timeout padrão de 30 segundos. O
valor pode ser alterado, por exemplo:

```bash
PYDOS_TEST_TIMEOUT=10s tests/run_dos_linux.sh all linq
```

## Estratégia recomendada

1. Use o emu2 em cada alteração para a suíte 8086: ele inicia quase
   instantaneamente, captura `stdout` e retorna o `ERRORLEVEL` ao Linux.
2. Use o DOSEMU2 em modo `-quiet -3` para a suíte 386/CauseWay e também como uma
   segunda implementação de DOS para o 8086.
3. Mantenha uma suíte menor em QEMU + FreeDOS para validar comportamento de
   kernel, PSP, memória segmentada e hardware que emuladores de chamadas DOS
   podem simplificar.

O runner usa somente as entradas ativas de `runtests.bat`. Para investigar um
teste comentado, informe seu nome explicitamente na linha de comando.
