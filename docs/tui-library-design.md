# Projeto da biblioteca TUI

Este documento define a próxima geração de `pydos.io.tui`. Ele parte de
medições feitas sobre o código atual, não de preferência estética, e registra
as restrições reais do compilador, do formato executável e do DOS que a API
precisa respeitar.

A decisão de escopo é uma API nova com quebra limpa: os testes `tui312`,
`tuiwid` e os samples são reescritos junto com a biblioteca, sem camada de
compatibilidade.

## 1. Diagnóstico da implementação atual

### O widget layer não é usado pelo sample principal

`samples/edit` tem 596 linhas e importa apenas `Canvas`, `Dialog`, `Key`,
`Screen`, `TextInput` e `wait_key`. Menus, foco, sombra, barra de status,
scrollbar, realce de busca e redesenho incremental são reescritos à mão com
`screen.write()` direto. Isso não é preferência do autor do sample: é a
consequência de `Canvas` não ter atributo por célula.

`Canvas` é uma `list[str]`. `Screen.present(canvas, fg, bg)` pinta o canvas
inteiro com um único par de cores. Qualquer variação de cor exige sair da
composição em memória e escrever direto na tela. Uma UI colorida, portanto,
não pode ser expressa como composição — apenas como sequência de escritas.

### O custo de desenho é proporcional ao número de caracteres

`pydos_tui_write_at` (runtime/pdos_tui.c:194) executa, por caractere:

```c
bios_set_cursor(x, y);        /* INT 10h AH=02 */
bios_write_cell(ch, attr);    /* INT 10h AH=09 */
```

Um quadro cheio 80×25 custa **4000 interrupções BIOS**. Não há escrita direta
em memória de vídeo, nem buffer duplo, nem comparação com o conteúdo anterior.

### Orçamento de tamanho no 8086

Medido neste repositório, alvo 8086:

| binário | tamanho | conteúdo |
|---|---:|---|
| `hello_project.exe` | 139 KB | linha de base do runtime |
| `tui_demo.exe` | 234 KB | +95 KB para 530 linhas de TUI |
| `alley_cat.exe` | 322 KB | |
| `edit.exe` | **483 KB** | ~75% dos 640 KB convencionais |

Isso dá aproximadamente **180 bytes de executável por linha de Python**. Uma
biblioteca de 3000 linhas totalmente linkada custaria cerca de 540 KB e não
caberia junto com o DOS e o programa.

O compilador não elimina funções não alcançáveis de módulos linkados por
fonte: `tui_demo` incorpora `TextInput` e `Dialog` completos sem usá-los.
Trocando a fachada `pydos/io/tui/__init__.py` por uma versão enxuta, o mesmo
programa caiu de 816 KB para 547 KB de assembly e de 64 para 48 procedimentos,
sem nenhuma outra alteração.

### Outras limitações medidas

- `Application.add()` só registra `Button` como focável, então `TextInput`
  nunca recebe foco pelo Tab;
- o teclado usa INT 21h AH=08, que não informa Ctrl/Alt/Shift nem F11/F12;
- não há mouse;
- `delay_ms` faz espera ocupada em INT 21h AH=2Ch, sem ceder tempo;
- 80×25 está fixo no C, ignorando a BIOS data area, 80×43 e 80×50;
- as bordas usam `+`, `-` e `|` em vez de CP437;
- widgets não têm layout, hierarquia, z-order nem modal.

## 2. Restrições que a API precisa respeitar

Verificadas compilando e executando sob DOSEMU, não presumidas.

**Só `from x import y` linka.** `link_source_imports()` (compiler/main.cpp)
trata apenas `AST_IMPORT_FROM`. `import helper` compila em silêncio e produz
código quebrado: o corpo do módulo não entra no executável. A API não pode
depender de `import pydos.io.tui as tui`.

**Namespace achatado, teto de 64 módulos.** Os corpos dos módulos são
concatenados num único espaço global (`seen[64][128]` em main.cpp). Nomes de
classe precisam ser distintos no programa inteiro e a granularidade de módulos
consome esse orçamento.

**Sem format spec em f-string.** `f"{x:>5}"` não faz parse. Alinhamento usa
`ljust`, `rjust` e `center`.

**Sem `f(*args)` no call site.** `*args` na definição funciona; encaminhar
não. Sinais e despachantes precisam de aridade fixa.

**Sem atribuição em fatia de `bytearray`.** `buf[2:4] = ...` não compila;
índice simples e leitura de fatia funcionam.

**CP437 não sobrevive à comparação de saída.** `chr(218)` é um byte no PyDOS,
mas o CPython escreve UTF-8 de U+00DA (`c3 9a`) e o DOSEMU traduz o byte 0xDA
para U+250C (`e2 94 8c`). Testes golden que imprimem o buffer precisam usar
o conjunto ASCII de bordas ou emitir códigos numéricos.

**Tipagem é obrigatória na stdlib.** Anotar habilita os caminhos
especializados do compilador. Isso só é seguro agora porque a especialização
de tipo foi corrigida (seção 7).

## 3. Princípios

1. **C só faz o que Python não pode.** Transferência para memória de vídeo,
   INT 10h/16h/21h/33h, tempo. Composição, layout, foco, tema e política de
   evento são Python compilado.
2. **Atributo por célula desde a base.** Se a estrutura de composição não
   carrega cor, todo widget acaba escrevendo direto na tela.
3. **Custo por quadro proporcional ao que mudou**, não ao tamanho da tela.
4. **Toda a árvore de widgets é testável sem tela.**
5. **O que o programa não usa não entra no executável.**
6. **Tudo anotado.**

## 4. Camadas

```text
aplicação
   |
widgets + layout + foco + tema        (Python)
   |
buffer + eventos + tela               (Python)
   |
primitivas DOS                        (C, pdos_tui.c)
   |
BIOS / DOS
```

## 5. Primitivas C

Substituem `pydos_tui_write_at`. Todas compilam nos dois alvos e continuam
compilando no host através de `compat/dos.h`.

### Vídeo

```c
_pydos_tui_probe() -> int          /* colunas, linhas e mono da BIOS data area */
_pydos_tui_present(glyphs, attrs, x, y) -> None
_pydos_tui_fill(x, y, w, h, ch, attr) -> None
_pydos_tui_scroll(x, y, w, h, lines, attr) -> None
_pydos_tui_cursor(x, y) -> None
_pydos_tui_cursor_shape(kind) -> None
_pydos_tui_set_rows(rows) -> int   /* 25, 43 ou 50 */
_pydos_tui_blink(enabled) -> None  /* INT 10h AX=1003h: 16 cores de fundo */
_pydos_tui_save_video() -> int
_pydos_tui_restore_video(state) -> None
_pydos_tui_vsync() -> None         /* porta 0x3DA, para jogos e CGA */
```

`_pydos_tui_present` recebe duas listas de strings — o plano de glifos e o
plano de atributos, uma string por linha — intercala em memória de vídeo e
**escreve apenas as células que diferem do conteúdo atual**. Um quadro cheio
passa de 4000 interrupções para uma chamada C com comparação e cópia.

Endereçamento: no 8086, ponteiro far para `0xB800` (ou `0xB000` em mono),
renormalizado por linha para não cruzar limite de segmento. No 386 sob
CauseWay o primeiro megabyte é mapeado linearmente, então `0xB8000` é
alcançável pelo seletor plano; a implementação testa esse acesso e cai para
INT 31h AX=0002 se falhar.

### Teclado

```c
_pydos_tui_key_event() -> int      /* -1 sem tecla; senão scancode<<8 | ascii
                                      com modificadores nos bits 16..18 */
_pydos_tui_shift_state() -> int    /* INT 16h AH=12h */
```

Usa INT 16h AH=11h/10h (teclado estendido) e cai para AH=01h/00h quando a BIOS
não suporta, preservando o 8086 real. Isso entrega Ctrl/Alt/Shift, F11 e F12,
que a interface atual não consegue distinguir.

### Mouse

```c
_pydos_tui_mouse_init() -> int     /* INT 33h AX=0000; 0 sem driver */
_pydos_tui_mouse_poll() -> int     /* x, y, botões, press e release empacotados */
_pydos_tui_mouse_show(visible) -> None
```

Usa os contadores de press/release (AX=0005/0006) para não perder cliques
entre quadros.

### Tempo

```c
_pydos_tui_ticks_ms() -> int
_pydos_tui_sleep_ms(ms) -> None    /* cede a fatia com INT 2Fh AX=1680h */
```

O empacotamento em inteiro nas duas funções de polling é deliberado: elas são
chamadas a cada quadro e não devem alocar. A decodificação em objeto acontece
só quando existe evento.

## 6. API Python

### Geometria, cor e estilo

```python
@dataclass
class Rect:
    x: int = 0
    y: int = 0
    width: int = 0
    height: int = 0

    def contains(self, px: int, py: int) -> bool: ...
    def inset(self, amount: int) -> "Rect": ...
    def split_top(self, rows: int) -> "tuple": ...

@dataclass
class Style:
    fg: int = Color.LIGHT_GRAY
    bg: int = Color.BLACK
    blink: bool = False

    def attr(self) -> int: ...
    def inverted(self) -> "Style": ...
```

`Style` é valor imutável por convenção e sabe se converter no byte de atributo
do modo texto. `Theme` mapeia nome semântico para `Style`, e não a UI para
números de cor:

```python
theme = Theme.turbo()
buffer.text(2, 1, "Salvar", theme.style("button.focus"))
```

### Buffer

Dois planos paralelos de `str` — glifos e atributos. A escolha é medida: fatia
e concatenação de `str` são primitivas C, então pintar uma linha inteira custa
poucas operações de runtime, e o buffer continua trivialmente inspecionável.

```python
buffer = Buffer(80, 25)
buffer.box(Rect(0, 0, 80, 25), theme.style("window"), title="EDIT")
buffer.text(2, 2, "Olá", theme.style("text"))
buffer.blit(other, x=10, y=4)
with buffer.clip(Rect(1, 1, 78, 23)):
    ...
print(buffer)            # plano de glifos
print(buffer.attr_map()) # plano de atributos em hexadecimal
```

`Border.SINGLE`, `DOUBLE`, `HEAVY` e `ASCII` vivem em `glyphs.py`.
`Border.ASCII` existe para os testes golden.

### Tela

```python
with Screen() as screen:      # restaura modo, cursor e cores mesmo com exceção
    screen.present(buffer)    # uma chamada C, só as células alteradas
    screen.cursor(x, y, Cursor.UNDERLINE)
```

### Teclas e eventos

```python
@dataclass
class Key:
    code: int = 0
    name: str = ""
    ctrl: bool = False
    alt: bool = False
    shift: bool = False

    def __eq__(self, other): ...   # compara com str e com int
```

`Key.__eq__` aceitando `str` só funciona porque o despacho de `__eq__` entre
tipos diferentes foi corrigido (seção 7). Com isso:

```python
if event.key == "ctrl+s":
    self.save()
elif event.key == Key.ESCAPE:
    self.close()
```

Atalhos declarativos, sem depender de `f(*args)`:

```python
class Editor(App):
    title = "EDIT"
    bindings = {
        "ctrl+s": "save",
        "f3": "find",
        "escape": "quit",
    }
```

### Widgets e layout

Composição declarativa por construtor, que o compilador aceita via `*args`:

```python
class Editor(App):
    def build(self) -> Widget:
        self.path = TextInput(placeholder="DOCUMENT.TXT")
        return VBox(
            MenuBar(self.menus()),
            HBox(FileList(), TextArea(), weights=(1, 3)),
            StatusBar(),
        )
```

Widgets previstos: `Label`, `Button`, `CheckBox`, `RadioGroup`, `TextInput`,
`TextArea`, `ListView`, `Table`, `ProgressBar`, `ScrollBar`, `Frame`,
`MenuBar`, `ContextMenu`, `StatusBar`, `Dialog`, `MessageBox`, `InputBox`,
`FileDialog`.

`Widget` expõe `rect`, `visible`, `enabled`, `focusable`, `style`, e os pontos
`on_mount()`, `render(buffer)`, `on_key(event)`, `on_mouse(event)`. Uma
propriedade que muda o estado visível marca o widget como sujo:

```python
@property
def text(self) -> str:
    return self._text

@text.setter
def text(self, value: str) -> None:
    self._text = value
    self.invalidate()
```

Manipuladores continuam sendo funções de primeira classe, inclusive fábricas
definidas dentro de métodos — padrão que só passou a funcionar com as
correções da seção 7:

```python
def make_item_handler(self, item: str):
    def handler() -> None:
        self.select(item)
    return handler
```

## 7. Correções de compilador e runtime já aplicadas

O projeto acima depende de comportamentos que não funcionavam. Todos foram
corrigidos e cobertos por testes de regressão nesta rodada:

| Defeito | Efeito | Correção |
|---|---|---|
| `def` aninhado em método não gerava objeto função | `TypeError: object is not callable` | `build_funcdef` limpa o marcador de classe durante o corpo |
| `str * int` com tipo conhecido virava multiplicação inteira | `"ab" * 3` produzia lixo | dispatch de aritmética centralizado em `arith_dispatch_func` |
| Identidades `*0`, `*1`, `*2`, `+0`, `-0` aplicadas a não numéricos | `"ab" * 0` dava `0`; `"ab" + 0` perdia o `0` | guarda por tipo provado |
| `tuple * int` recusado; concat e repeat ausentes no runtime | erro de semântica e `TypeError` | regra de sema + `pydos_seq_repeat`, `pydos_tuple_concat` |
| Nomes aninhados iguais colidiam no assembly | `E600: symbol already defined` | símbolo qualificado pelo escopo, com desambiguação |
| Callee avaliado depois dos argumentos | `obj.make()(5)` não chamava | callee construído antes do push |
| Chamada de variável capturada | `TypeError: object is not callable` | chamada indireta pela célula |
| Captura transitiva (avô), inclusive `nonlocal` | valor errado ou zero | propagação pelos escopos intermediários |
| Resultado descartado escrito em slot negativo | corrompia o primeiro local do chamador | `store_*_to_temp` ignora destino ausente |
| `@staticmethod` tipava o 1º parâmetro como a classe | `Style.pack(fg, bg)` não compilava | sema respeita o decorador |
| `__eq__` não despachado entre tipos diferentes | `key == "ctrl+s"` sempre falso | dispatch antes da checagem de tipo |
| `__lt__` refletido ausente no caminho de vtable | `sorted()` não ordenava | fallback refletido em `compare_instance` |

Testes adicionados: `seqops`, `mthclos`, `callfrm`, `nstname`, `clsrdep`,
`richcmp`. Todos comparam byte a byte com a saída do CPython.

## 8. Tamanho e modularidade

Duas frentes, ambas necessárias:

**Fachada enxuta e import explícito.** `pydos/io/tui/__init__.py` exporta
somente o núcleo imediato — `Color`, `Style`, `Rect`, `Buffer`, `Screen`,
`Key`. Widgets vêm do seu próprio módulo. Medido: −33% de assembly só com
isso.

**Eliminação de função morta no compilador.** Hoje toda função de todo módulo
linkado por fonte vai para o executável. Um passe de alcançabilidade a partir
do `__init__` do módulo, com as mesmas raízes que `pirmrg.cpp` já usa para a
stdlib pré-compilada, remove o resto. O ganho vale para todo programa PyDOS,
não só para a TUI.

Com as duas frentes, um aplicativo típico linka de 800 a 1200 linhas de TUI,
ou seja de 150 a 220 KB — dentro do orçamento do 8086 mesmo com um documento
grande em memória.

## 9. Testes

O ponto central: **`Buffer` é Python puro e imprimível**, então a árvore de
widgets inteira é verificável sem tela, dentro do harness atual de `.exp`.

```python
buffer = Buffer(20, 5)
Button("OK", on_click=noop).render(buffer)
print(buffer)             # golden do plano de glifos
print(buffer.attr_map())  # golden do plano de atributos
```

Para aplicações completas, um driver sem tela e uma fonte de eventos roteirizada:

```python
app = Editor(screen=HeadlessScreen(80, 25),
             input=ScriptedInput(["ctrl+o", "a", "enter", "escape"]))
app.run()
print(app.screen.buffer)
```

Isso torna determinístico o que hoje só é verificável olhando a janela do
DOSEMU: foco, navegação de menu, modal, edição e atalhos.

Regras dos testes golden:
- usar `Border.ASCII`, porque bytes CP437 não sobrevivem à comparação;
- gerar o `.exp` com CPython sempre que o teste não depender de DOS;
- manter os testes interativos do DOSEMU apenas para o driver real.

## 10. Plano

Cada fase termina com `tests/run_dos_linux.sh all` verde nos dois alvos.

**Fase 0 — compilador.** Eliminação de função morta para módulos linkados por
fonte; teto de módulos de 64 para 256; erro explícito para `import x` em vez
de código quebrado silencioso. Métrica: tamanho de `edit.exe` antes e depois.

**Fase 1 — primitivas C.** `pdos_tui.c` reescrito com a seção 5. Validação:
teste nativo dos empacotamentos e um teste DOS que escreve, lê de volta e
confere células.

**Fase 2 — núcleo.** `geometry`, `color`, `glyphs`, `buffer`, `screen`,
`clock`, tipados. Golden tests do buffer.

**Fase 3 — entrada.** `keys`, `events`, `input`, mouse, `ScriptedInput`.

**Fase 4 — framework.** `widget`, `focus`, `layout`, `theme`, `app`, com
`HeadlessScreen`. Testes de aplicação roteirizada.

**Fase 5 — widgets.** Na ordem de uso dos samples: `Label`, `Button`,
`Frame`, `TextInput`, `ListView`, `MenuBar`, `Dialog`, `StatusBar`,
`ScrollBar`, `TextArea`, `Table`.

**Fase 6 — samples.** `edit` reescrito sobre widgets de verdade, `alley_cat`
sobre `Buffer`, `tui_demo` como vitrine. Remoção da API antiga e dos testes
`tui312` e `tuiwid`, substituídos pelos golden novos.

**Fase 7 — documentação.** Guia de uso e atualização do `README.md`.

## 11. Riscos

- **Acesso a vídeo no 386.** O caminho linear sob CauseWay precisa ser
  confirmado no DOSEMU e em hardware; o fallback por seletor DPMI é o plano B
  e deve ser escrito junto, não depois.
- **INT 16h estendido em 8086 real.** A detecção precisa de fallback para a
  BIOS antiga; DOSEMU não prova esse caminho.
- **Mouse ausente.** Toda a API de mouse é opcional por construção; nenhum
  widget pode depender dela.
- **Teto de 64 módulos.** Aumentar o limite é pré-requisito da granularidade
  proposta.
- **Tamanho no 8086.** Se a eliminação de função morta não entrar, a Fase 5
  precisa parar em `Table` e `TextArea`, ou esses widgets ficam restritos ao
  386.
