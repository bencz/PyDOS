# Matriz completa de módulos Python 3.12 para MS-DOS

Esta matriz parte do
[índice oficial de módulos Python 3.12](https://docs.python.org/3.12/py-modindex.html),
não apenas dos módulos lembrados durante a implementação. Ela registra a
viabilidade inicial no PyDOS e deve ser refinada por API antes de cada módulo
ser iniciado.

## Classes de viabilidade

| Código | Significado |
|---|---|
| P | majoritariamente implementável em Python após completar o núcleo |
| D | implementável, mas exige uma primitiva pequena ou adaptação específica do DOS |
| X | depende de biblioteca externa; opcional e possivelmente apenas 386 |
| N | depende de uma pilha de rede DOS; não pertence à baseline |
| C | concorrência/processos incompatíveis ou fortemente limitados no 8086 |
| H | serviço interno do runtime CPython; requer uma alternativa própria ou não se aplica |
| O | específico de outro sistema operacional/interface indisponível |
| G | interface gráfica sem backend DOS definido |
| R | removido em 3.12 ou depreciado; baixa prioridade/não implementar por padrão |

“P” não significa simples nem já suportado. `decimal`, por exemplo, é Python
portável, mas depende primeiro de big integers. “X” também não significa
impossível: significa que não pode ser prometido pela runtime mínima.

## P — portáveis, candidatos à stdlib em Python

`__future__`, `__main__`, `abc`, `argparse`, `base64`, `bisect`, `builtins`,
`calendar`, `cmd`, `collections`, `collections.abc`, `colorsys`,
`configparser`, `contextlib`, `copy`, `copyreg`, `csv`, `dataclasses`,
`decimal`, `difflib`, `email`, `email.charset`, `email.contentmanager`,
`email.encoders`, `email.errors`, `email.generator`, `email.header`,
`email.headerregistry`, `email.iterators`, `email.message`, `email.mime`,
`email.mime.application`, `email.mime.audio`, `email.mime.base`,
`email.mime.image`, `email.mime.message`, `email.mime.multipart`,
`email.mime.nonmultipart`, `email.mime.text`, `email.parser`, `email.policy`,
`email.utils`, `enum`, `filecmp`, `fileinput`, `fnmatch`, `fractions`,
`functools`, `getopt`, `gettext`, `glob`, `graphlib`, `heapq`, `html`,
`html.entities`, `html.parser`, `http`, `http.cookiejar`, `http.cookies`,
`ipaddress`, `itertools`, `json`, `json.tool`, `keyword`, `linecache`,
`logging`, `logging.config`, `logging.handlers`, `mailbox`, `mimetypes`,
`netrc`, `numbers`, `operator`, `os.path`, `pathlib`, `pickle`,
`pickletools`, `platform`, `plistlib`, `pprint`, `profile`, `pstats`,
`pyclbr`, `quopri`, `random`, `reprlib`, `sched`, `shelve`, `shlex`,
`shutil`, `statistics`, `string`, `tarfile`, `tempfile`, `textwrap`,
`timeit`, `token`, `tokenize`, `tomllib`, `traceback`, `types`, `typing`,
`unittest`, `unittest.mock`, `urllib`, `urllib.error`, `urllib.parse`,
`urllib.response`, `urllib.robotparser`, `uuid`, `warnings`, `wave`,
`wsgiref`, `wsgiref.handlers`, `wsgiref.headers`, `wsgiref.types`,
`wsgiref.util`, `wsgiref.validate`, `xdrlib`, `xml`, `xml.dom`,
`xml.dom.minidom`, `xml.dom.pulldom`, `xml.etree.ElementInclude`,
`xml.etree.ElementTree`, `xml.sax`, `xml.sax.handler`, `xml.sax.saxutils`,
`xml.sax.xmlreader`, `xmlrpc`, `xmlrpc.client`, `zipapp` e `zoneinfo`.

Dependências importantes dentro desse grupo:

- `decimal`, `fractions`, `ipaddress` e partes de `statistics` precisam de
  inteiros arbitrários e aritmética numérica correta;
- `email`, `html`, `urllib.parse`, XML, `stringprep` e `zoneinfo` dependem de
  Unicode/codecs completos;
- `pickle`, `pkgutil`, `runpy`, `site` e módulos de recursos dependem do
  sistema de imports/packages;
- `unittest`, `typing`, `dataclasses`, `abc` e `enum` pressionam o modelo de
  classes, descriptors e reflection;
- partes de `wsgiref`, `urllib.response` e `http.cookiejar` são portáveis,
  mas os transports/servidores continuam condicionados à rede.

## D — viáveis com primitivas DOS ou suporte do runtime

`array`, `atexit`, `binascii`, `cmath`, `codecs`, `datetime`, `encodings`,
`encodings.idna`, `encodings.utf_8_sig`, `errno`, `faulthandler`, `gc`,
`getpass`, `io`, `locale`, `marshal`, `math`, `os`, `signal`, `stat`,
`struct`, `sys`, `time`, `tracemalloc`, `unicodedata`, `weakref` e
`xml.parsers.expat`, `xml.parsers.expat.errors` e `xml.parsers.expat.model`.

Esses módulos devem usar C somente para a primitiva inevitável: interrupções
DOS, relógio, arquivos/devices, `libm`, layout binário, console, GC, weakrefs
ou uma tabela Unicode compacta. A API pública e algoritmos permanecem Python.

## X — bibliotecas externas/opcionais

`audioop`, `bz2`, `ctypes`, `dbm`, `dbm.dumb`, `dbm.gnu`, `dbm.ndbm`,
`gzip`, `hashlib`, `hmac`, `lzma`, `secrets`, `sqlite3`, `ssl`, `zlib`,
`zipfile` e `zipimport`.

- `sqlite3` exige uma porta de SQLite e memória suficiente; não integra a
  baseline 8086 e só deve ser estudado como pacote opcional, provavelmente
  386.
- `ssl` exige biblioteca criptográfica, entropia e sockets; não integra a
  baseline.
- `bz2`, `lzma` e `zlib` podem existir como pacotes opcionais se houver portas
  Open Watcom adequadas. `gzip` e `zipfile` dependem delas.
- hashes simples podem ser Python puro, mas `hashlib` completo e `secrets`
  seguro exigem primitives/entropia que o DOS não fornece por padrão.
- `ctypes` não pode assumir ABI/loader de CPython; exigiria um FFI próprio e
  modelos distintos para 16 e 32 bits.

## N — rede opcional, fora da baseline

`asyncio` (transportes), `ftplib`, `http.client`, `http.server`, `imaplib`,
`poplib`, `select`, `selectors`, `smtplib`, `socket`, `socketserver`,
`urllib.request`, `webbrowser`, `wsgiref.simple_server`, `xmlrpc.server` e os
clientes/servidores que dependem deles.

Sockets não são parte nativa uniforme do MS-DOS. Uma futura camada pode usar
uma pilha como Watt-32, mTCP ou um driver do emulador, mas isso seria um
backend opcional com testes próprios. Compatibilidade 8086, disponibilidade
de memória e diferenças entre emulador/hardware precisam ser avaliadas antes.

## C — concorrência, processos e IPC

`_thread`, `concurrent`, `concurrent.futures`, `contextvars`,
`multiprocessing`, `multiprocessing.connection`, `multiprocessing.dummy`,
`multiprocessing.managers`, `multiprocessing.pool`,
`multiprocessing.shared_memory`, `multiprocessing.sharedctypes`, `queue`,
`subprocess` e `threading`.

O DOS real mode não fornece threads ou o modelo de processos/pipes esperado
por CPython. `subprocess` pode futuramente expor um subconjunto DOS de
`spawn/exec`, e `contextvars` pode ser adaptado ao scheduler cooperativo, mas
isso não equivale à API completa. Não criar stubs que aparentem funcionar.

## H — acoplados ao interpretador/runtime CPython

`ast`, `bdb`, `code`, `codeop`, `compileall`, `cProfile`, `dis`, `doctest`,
`ensurepip`, `importlib`, `importlib.abc`, `importlib.machinery`,
`importlib.metadata`, `importlib.resources`, `importlib.resources.abc`,
`importlib.util`, `inspect`, `lib2to3`, `modulefinder`, `pdb`, `pkgutil`,
`py_compile`, `pydoc`, `runpy`, `site`, `sitecustomize`, `symtable`,
`sys.monitoring`, `sysconfig`, `test`, `test.regrtest`, `test.support`,
`test.support.bytecode_helper`, `test.support.import_helper`,
`test.support.os_helper`, `test.support.script_helper`,
`test.support.socket_helper`, `test.support.threading_helper`,
`test.support.warnings_helper`, `trace`, `usercustomize` e `venv`.

Alguns nomes terão equivalentes PyDOS — por exemplo `ast`, `inspect`, `pdb`,
`importlib` e `trace` — mas não podem simplesmente copiar pressupostos de
bytecode, frames e import machinery do CPython. Precisam de especificação
própria compatível na superfície útil.

## O — específicos de Unix, Windows ou outro sistema

`crypt`, `curses`, `curses.ascii`, `curses.panel`, `curses.textpad`, `fcntl`,
`grp`, `mmap`, `msilib`, `msvcrt`, `encodings.mbcs`, `nis`, `ossaudiodev`, `pipes`, `posix`,
`pty`, `pwd`, `readline`, `resource`, `rlcompleter`, `spwd`, `syslog`,
`termios`, `tty`, `winreg` e `winsound`.

Uma API DOS equivalente pode ser criada em módulos próprios, mas esses nomes
não devem alegar compatibilidade quando os conceitos do sistema original não
existirem. `mmap` merece reavaliação apenas no alvo 386/DPMI.

## G — interfaces gráficas sem backend DOS

`_tkinter`, `idlelib`, `tkinter`, `tkinter.colorchooser`,
`tkinter.commondialog`, `tkinter.dnd`, `tkinter.filedialog`, `tkinter.font`,
`tkinter.messagebox`, `tkinter.scrolledtext`, `tkinter.simpledialog`,
`tkinter.tix`, `tkinter.ttk`, `turtle` e `turtledemo`.

Só são viáveis após escolher e implementar um backend gráfico DOS. Não fazem
parte da baseline de console.

## R — removidos/depreciados ou de baixíssima prioridade

Removidos em Python 3.12: `asynchat`, `asyncore`, `distutils`, `imp` e
`smtpd`. Não implementar.

Depreciados na linha 3.12: `aifc`, `audioop`, `cgi`, `cgitb`, `chunk`,
`crypt`, `imghdr`, `lib2to3`, `mailcap`, `nis`, `nntplib`, `optparse`,
`ossaudiodev`, `pipes`, `sndhdr`, `spwd`, `sunau`, `telnetlib`, `uu` e
`xdrlib`. Só implementar se uma aplicação real do PyDOS justificar.

## Ordem prática para o PyDOS

1. Fechar builtins, tipos, exceções e fundamentos do roadmap principal.
2. Implementar imports/packages e os módulos D mínimos: `sys`, `os`, `io`,
   `errno`, `time`, `math`, `struct`, `codecs` e `gc`.
3. Portar primeiro módulos P pequenos e independentes: `operator`, `bisect`,
   `heapq`, `collections`, `functools`, `itertools`, `contextlib`, `string`,
   `textwrap`, `base64`, `json`, `configparser` e `argparse`.
4. Expandir arquivos/caminhos, dados e testes.
5. Avaliar separadamente cada backend X/N/C, com feature flags por alvo. A
   ausência desses backends não deve impedir a compatibilidade do núcleo DOS.
