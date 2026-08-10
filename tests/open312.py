caminho = "IOTEXT.TMP"

with open(caminho, "w", encoding="utf-8") as arquivo:
    arquivo.write("primeira linha\nsegunda linha\n")

try:
    with open(caminho, "r", encoding="utf-8") as arquivo:
        for numero, linha in enumerate(arquivo, start=1):
            linha = linha.strip()
            print(f"Linha {numero}: {linha}")

    print("Arquivo fechado automaticamente.")

except FileNotFoundError:
    print(f"O arquivo '{caminho}' não foi encontrado.")

except OSError as erro:
    print(f"Erro ao abrir ou ler o arquivo: {erro}")

try:
    with open("NAOEXIST.TXT", "r", encoding="utf-8") as arquivo:
        print(arquivo.read())
except FileNotFoundError:
    print("Arquivo ausente tratado.")
