# Guia passo a passo — gerar dados de treino na nuvem (GCP), do zero

Para quem **nunca usou o Google Cloud**. O objetivo: ligar uma máquina potente de 60
núcleos por ~25 minutos, ela gera milhões de posições de xadrez, sobe num "balde" de
armazenamento, e **se desliga sozinha** (para não cobrar à toa). Depois você baixa o
arquivo e treina a rede no seu PC (de graça, na sua RTX 4050).

Custo total de uma rodada: **menos de US$1** (uns R$5). Leia até o fim antes de
começar — tem um aviso de custo no final.

> Termos rápidos: **VM/instância** = um computador alugado na nuvem. **Spot/preemptível**
> = VM bem mais barata que pode ser desligada pelo Google a qualquer hora (ok pro
> nosso uso). **Bucket** = uma pasta de arquivos na nuvem. **vCPU** = núcleo de CPU.

---

## Parte 0 — Pré-requisitos (uma vez só)

1. **Conta Google** com faturamento. Acesse <https://console.cloud.google.com> e
   faça login. Se for a primeira vez, o Google oferece **créditos grátis** (US$300)
   — mais que suficiente; ainda assim siga o aviso de custo no fim.
2. **Cartão de crédito** cadastrado em "Faturamento" (Billing). O Google exige, mas
   spot + autodelete mantêm o gasto em centavos.

---

## Parte 1 — Criar um projeto

1. No topo do console, clique no seletor de projetos (ao lado de "Google Cloud").
2. **"Novo projeto"** → nome: `chess-engine` → **Criar**.
3. Espere ~30s e selecione o projeto `chess-engine` no seletor.

---

## Parte 2 — Ativar as APIs necessárias

1. Barra de busca do console → digite **"Compute Engine API"** → abra → **Ativar**.
   (Demora 1–2 min na primeira vez.)
2. Busque **"Cloud Storage API"** → **Ativar**.

---

## Parte 3 — Instalar o `gcloud` (a ferramenta de linha de comando) no seu PC

O jeito mais simples no Windows:

1. Baixe o instalador: <https://cloud.google.com/sdk/docs/install> → "Google Cloud
   CLI installer" (Windows).
2. Rode o instalador (aceite o padrão, deixe marcado "Run gcloud init").
3. Quando abrir o terminal do `gcloud init`:
   - Faça login (abre o navegador, escolha sua conta Google).
   - Escolha o projeto **chess-engine**.
   - Região: digite `us-central1` quando perguntar a região/zona padrão
     (escolha `us-central1-a` para a zona). É barato e tem disponibilidade boa.
4. Confirme que funciona — abra um **novo** PowerShell e rode:
   ```powershell
   gcloud --version
   gcloud config get-value project        # deve mostrar: chess-engine
   ```

---

## Parte 4 — Criar o "balde" (bucket) de armazenamento

Escolha um nome **único no mundo** (troque `joao` por algo seu):
```powershell
gcloud storage buckets create gs://joao-chess-nnue --location=us-central1
```
Se reclamar que o nome existe, troque por outro. **Anote esse nome** — vamos usá-lo.

---

## Parte 5 — Preparar o script de inicialização

O arquivo `tools/cloud/startup.sh` (neste repo) é o que a VM executa sozinha. Você
precisa editar **duas linhas** no topo dele:

```bash
BUCKET="gs://joao-chess-nnue"     # <-- o nome do SEU bucket da Parte 4
NET_URL=""                         # <-- deixe vazio na primeira rodada (rotula com a HCE)
```

E confirme que a linha do `git clone` aponta para o repositório certo:
```bash
git clone --depth 1 https://github.com/Foxer131/ChessEngine.git engine
```
(Já vem assim. A VM baixa o código daqui para compilar o `gen_data`.)

Salve o arquivo.

---

## Parte 6 — Ligar a máquina (o "burst")

Um único comando cria a VM de **60 núcleos spot**, que roda o script e se deleta no
fim. No PowerShell, **dentro da pasta do projeto** (onde está `tools/cloud/`):

```powershell
gcloud compute instances create chess-datagen `
  --zone=us-central1-a `
  --machine-type=c2d-standard-60 `
  --provisioning-model=SPOT `
  --instance-termination-action=DELETE `
  --image-family=debian-12 --image-project=debian-cloud `
  --scopes=storage-read-write `
  --metadata-from-file=startup-script=tools/cloud/startup.sh
```

O que cada parte faz (resumido):
- `c2d-standard-60` = 60 vCPU. `--provisioning-model=SPOT` = barato.
- `--instance-termination-action=DELETE` = se o Google interromper, a VM some (não
  fica cobrando disco parado).
- `--scopes=storage-read-write` = deixa a VM gravar no seu bucket.
- `--metadata-from-file=startup-script=...` = manda o `startup.sh` para a VM rodar
  sozinha no boot.

> Se aparecer erro de **cota** (quota) de CPUs: contas novas às vezes têm limite < 60
> vCPU. Soluções: (a) peça aumento de cota no console (IAM & Admin → Cotas →
> "Compute Engine API, CPUs" → editar) — costuma ser aprovado em minutos; ou
> (b) use `c2d-standard-32` no comando acima (32 núcleos; gera na metade da
> velocidade, ainda barato).

---

## Parte 7 — Acompanhar o progresso

A VM demora ~2 min instalando/compilando, depois gera por ~25 min. Para ver o que
está acontecendo (log da "porta serial"):
```powershell
gcloud compute instances get-serial-port-output chess-datagen --zone=us-central1-a
```
Rode de novo a cada poucos minutos. Você verá "generating on 60 cores…", depois
"generated N positions; uploading…", e por fim "self-deleting".

Para checar se a VM já se deletou (= terminou):
```powershell
gcloud compute instances list
```
Quando `chess-datagen` **não** aparecer mais, acabou.

---

## Parte 8 — Baixar os dados e treinar no seu PC

```powershell
gcloud storage cp gs://joao-chess-nnue/all.txt C:\chess_sprt\data\all.txt
powershell -File tools\training\retrain.ps1
```
Isso normaliza, converte e treina na sua GPU (minutos). Depois copie a rede e teste:
```powershell
Copy-Item C:\chess_nnue\bullet\checkpoints\chessengine-30\quantised.bin C:\chess_sprt\data\net.nnue
powershell -File tools\training\sprt_nnue.ps1 -Net C:\chess_sprt\data\net.nnue -Nodes 20000
```

---

## Parte 9 — IMPORTANTE: confirmar que nada ficou ligado (evitar cobrança)

Spot + autodelete já cuidam disso, mas **sempre confira**:
```powershell
gcloud compute instances list                       # não deve listar chess-datagen
gcloud storage ls gs://joao-chess-nnue               # seus arquivos de dados
```
- A VM custa só enquanto está ligada (centavos por rodada). Se por algum motivo ela
  não se deletou, apague manualmente:
  ```powershell
  gcloud compute instances delete chess-datagen --zone=us-central1-a
  ```
- O bucket custa quase nada (uns centavos/mês por GB). Para zerar, apague os dados:
  ```powershell
  gcloud storage rm gs://joao-chess-nnue/all.txt
  ```
- Para garantir que nunca haja surpresa, crie um **orçamento com alerta**: console →
  "Faturamento" → "Orçamentos e alertas" → criar orçamento de, ex., R$20/mês com
  alerta por e-mail. Não bloqueia nada, mas avisa.

---

## Rodadas seguintes (bootstrapping — onde a força realmente cresce)

Depois que uma rede vencer a anterior no SPRT, suba ela pro bucket e use na próxima
geração como "professora" (rotula os dados com a rede, não com a HCE):
```powershell
gcloud storage cp C:\chess_sprt\data\net.nnue gs://joao-chess-nnue/net.nnue
```
e no `startup.sh` mude:
```bash
NET_URL="gs://joao-chess-nnue/net.nnue"
```
Lance o burst de novo. Cada rodada gera dados de um professor mais forte. Repita até
o ganho no SPRT estabilizar. Ver `docs/NNUE.md` para a lógica completa.
