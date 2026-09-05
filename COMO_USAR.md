# Trainer - Trails in the Sky the 2nd Chapter (Demo)

Trainer em C++ para `sora_2nd.exe`, com executável próprio e interface com
toggles, sliders e campos numéricos, no mesmo estilo do trainer de
DragonSword: Awakening.

Diferente daquele (que era Python + padrões vindos do WeMod), aqui **não existia
nenhuma tabela pronta**: as assinaturas, os offsets das structs e os pontos de
hook foram extraídos por engenharia reversa do próprio `sora_2nd.exe`.

---

## 1. Antes de tudo: o Windows Defender

O Defender **apaga o executável** por heurística de HackTool. Isso é falso
positivo e acontece com qualquer trainer: o programa abre o processo do jogo e
escreve na memória dele — é literalmente o comportamento que a heurística
procura. O código-fonte inteiro está em `src\`. Nada é baixado, nada roda em
segundo plano, nada toca em rede.

1. Rode **`ADICIONAR_EXCLUSAO_DEFENDER.bat`** (pede UAC). Ele adiciona só esta
   pasta `Trainer\` às exclusões.
2. Rode **`build.bat`** para recompilar, se o exe já tiver sido removido.
3. Rode **`INICIAR_TRAINER.bat`**.

---

## 2. Como usar

1. Abra o jogo normalmente pela Steam.
2. Abra o trainer (`INICIAR_TRAINER.bat`).
3. O selo no canto superior direito deve ficar verde:
   `CONECTADO PID xxxx - 10/10 assinaturas`.
4. **Carregue ou inicie uma partida.** Antes disso a save ainda não existe na
   memória e as opções que dependem dela não têm onde escrever — o rodapé avisa
   `Save ainda nao carregado`.
5. Marque as opções que quiser.

### Sliders e campos numéricos

Toda opção com multiplicador tem **slider + campo de digitação** lado a lado.
Eles ficam sincronizados: arraste o slider e o campo acompanha; digite no campo
e o slider acompanha. O campo aceita decimais (`12.5`) e o valor é limitado à
faixa válida da opção (1x a 100x). Use o slider para ajuste grosso e o campo
quando quiser um valor exato.

### Atalhos

`Ctrl+F1` a `Ctrl+F8` ligam/desligam as opções sem slider — a tecla aparece à
direita de cada opção. Usei `Ctrl` de propósito para não roubar as teclas F do
jogo.

O botão **"Desativar tudo e restaurar o jogo"** desmarca tudo e remove os
patches. Fechar o trainer faz a mesma coisa automaticamente.

---

## 3. O que cada opção faz

### Aba Combate

| Opção | Como funciona |
|---|---|
| **God Mode** | Hook em `battle::Object::AddHp`. Se o alvo do dano é da sua party, o delta vira 0. Cura continua funcionando normal. |
| **Redutor de Dano Recebido** | Mesmo hook: divide o dano que a party recebe. |
| **Multiplicador de Dano Causado** | Mesmo hook: multiplica o dano quando o alvo **não** é da party. |
| **Morte Instantânea** | Mesmo hook: força −999999 de HP em qualquer inimigo atingido. |
| **HP / EP / CP infinito** | Não é hook. O trainer relê a savedata ~40x por segundo e repõe o valor no máximo. Funciona dentro e fora de batalha, e revive quem estiver caído. |

As quatro primeiras compartilham um único trampolim. O trainer distingue party
de inimigo comparando o ponteiro do `battle::Status` do alvo com a faixa de
memória do array de personagens da savedata — personagens jogáveis vivem na
savedata, monstros são alocados por batalha.

### Aba Progressão

| Opção | Como funciona |
|---|---|
| **Multiplicador de EXP** | Hook no epílogo da função que calcula a EXP de cada personagem na tela de resultado. O valor em `eax` é multiplicado antes do `ret`, com teto de 999.999.999. |
| **Multiplicador de Sepith** | Hook em `savedata::Manager::AddItem`. Sepith são "itens" de id `0x136`–`0x13E`; o trainer multiplica a quantidade só quando o id cai nessa faixa. Pega batalha, baú e evento. Teto do jogo: 99.999 por tipo. |

### Aba Itens e Dinheiro

| Opção | Como funciona |
|---|---|
| **Multiplicador de Mira** | Hook na entrada de `savedata::Manager::AddMira`. Multiplica o valor recebido antes de somar. Só afeta ganhos — venda, missão, baú, evento; compras continuam custando o preço normal. Teto: 9.999.999. |
| **Multiplicador de Itens Recebidos** | Mesmo hook do sepith, no ramo dos ids que **não** são sepith. Vale para tudo que entra no inventário, inclusive compras. |
| **Mira Travada** | Escreve o valor do campo na savedata a cada ciclo. |
| **Sepith Travado** | Escreve os 7 tipos (terra, água, fogo, vento, tempo, espaço, miragem). |
| **Itens Infinitos** | Repõe a quantidade dos itens que você **já possui**. Não desbloqueia itens que você nunca pegou — de propósito, para não bagunçar a progressão nem a save. |

### Aba Party

Leitura ao vivo do array de personagens da savedata: slot, **nome**, ID, nível,
HP, EP, CP e EXP. O nome sai da datatable `NameTableData` do próprio jogo, então
acompanha o idioma e não depende de nenhuma lista fixa no trainer.
Linha verde = HP cheio, vermelha = caído.

---

## 4. Detalhes técnicos

Tudo abaixo foi obtido do `sora_2nd.exe` (x64, motor **fdk** da Falcom, com RTTI
e caminhos de `__FILE__` preservados no binário).

### Estruturas mapeadas

```
battle::Status  (0x2A0 bytes por personagem)
    +0x00  charaId        +0x04  level        +0x08  exp
    +0x0C  HP             +0x10  HP máximo
    +0x14  EP             +0x18  EP máximo
    +0x1C  CP             +0x20  CP máximo
    +0x248 slots de quartzo      +0x264 equipamento

savedata::Manager
    +0x1142F8  array de Status (slots 1..99, stride 0x2A0)
    +0x128A4C  tabela de itens: 5000 registros de 4 bytes (u16 qtd, u8 flags)
    +0x20B628  sepith[7]
    +0x20B644  sepith mass
    +0x20B648  mira
    +0x20B64C  medalhas (cassino)  <- NAO e a mira

battle::Manager
    +0x2448  ponteiro para o array de participantes da batalha
    +0x2450  quantidade

datatable::Manager
    +0x10 -> tabela "NameTableData": registro com u16 id em +0 e char* nome em +8
```

> **Cuidado com a mira.** O campo em `+0x20B64C` também tem teto 9.999.999 e
> também é somado/clampado igualzinho, mas dispara o evento `system.OnAddMedal`:
> é o contador de medalhas do cassino. A mira de verdade é `+0x20B648`, e o
> trainer pega esse offset direto da assinatura de `AddMira` justamente para não
> errar de campo.

### Pontos de hook

| Alvo | RVA nesta build | Bytes roubados |
|---|---|---|
| `battle::Object::AddHp` | `0x0E4900` | 5 |
| epílogo do cálculo de EXP | `0x0F103E` | 10 |
| `savedata::Manager::AddMira` | `0x43C7D0` | 11 |
| `savedata::Manager::AddItem` | `0x43C090` | 5 |

`AddHp` é o funil por onde passa **todo** dano e cura de combate:
`rcx` = objeto de batalha, `edx` = delta, `[rcx+0]` = `battle::Status` do alvo.
`AddItem` recebe `edx` = id e `r8d` = quantidade, e é por onde entram itens
**e** sepith.

### Assinaturas AOB

O trainer não usa endereços fixos: ele varre a seção de código e captura os
offsets direto das instruções (notação `sN` = N bytes capturados).

```
battle::Manager    48 8B 05 s4 48 8B 98 48 24 00 00 8B 80 50 24 00 00 48 8D 3C C3
savedata::Manager  48 8B 05 s4 0F B7 94 88 s4 48 8B CF
datatable::Manager 48 8B 3D s4 8B 13 48 8B 4F 10 48 8B 49 08
array de Status    48 69 C2 s4 48 05 s4 49 03 C2
AddMira            48 83 EC 28 44 8B 81 s4 41 B9 7F 96 98 00 44 03 C2
sepith             89 82 s4 44 8B 93 AC 5B 12 00
AddItem            48 89 6C 24 20 56 57 41 56 48 83 EC 60 48 8B 05 ?? ?? ?? ?? ...
TableFind          48 89 5C 24 08 44 8B 51 28 33 C0 48 8B 59 20 ...
Object::AddHp      48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 ...
cálculo de EXP     8B C5 48 8B AC 24 88 00 00 00 48 81 C4 90 00 00 00
```

Todas dão **exatamente 1 resultado** nesta build.

`TableFind` é um template instanciado várias vezes com layouts diferentes; a
assinatura casa a cópia literal exata usada pelo caminho da tabela de nomes, e
um único acerto já garante que os offsets do layout continuam válidos.

### Como o hook é instalado

1. `VirtualAllocEx` de uma code cave logo abaixo da base do módulo (para que
   todo `jmp rel32` entre cave e módulo caiba em ±2 GB).
2. As variáveis de controle (multiplicadores, flags, faixa da party) ficam nos
   primeiros 0x100 bytes da cave; a GUI só escreve nelas, nunca remonta código.
3. O trampolim é montado por um mini-assembler x64 (labels, `rel32`,
   endereçamento RIP-relativo) e escrito na cave.
4. Todas as threads do jogo são suspensas, o `jmp` é gravado no site e as
   threads voltam. Desligar restaura os bytes originais pelo mesmo caminho.

---

## 5. Se o jogo for atualizado

Rode `SoraSelfTest.exe ..\sora_2nd.exe`. Ele carrega a seção de código direto do
arquivo e mostra, para cada assinatura, quantos resultados encontrou e os
valores capturados. Também imprime os bytes dos quatro trampolins para
conferência no disassembler.

- `hits=1` em tudo: o trainer funciona sem mudança.
- `hits=0` em alguma: aquele recurso específico some (o trainer continua rodando
  com o resto). O selo mostra quantas das 10 assinaturas foram encontradas.
- `hits=2` ou mais: a assinatura ficou ambígua e precisa ser estendida em
  `src\engine.cpp`.

---

## 6. Cuidados

- **HP/EP/CP infinito, mira, sepith e itens escrevem na savedata em memória.**
  Se você salvar o jogo com eles ligados, os valores ficam gravados no save.
- Os multiplicadores (dano, EXP, mira, sepith, itens) não escrevem em lugar
  nenhum por conta própria: eles só alteram o número que o próprio jogo já ia
  somar. Desligou, volta ao normal na hora.
- O trainer só escreve em slots de personagem que passam na validação
  (nível 1–255, HP máximo 1–99999, HP ≤ HP máximo, etc.), para não corromper
  áreas vizinhas da save.
- Se `VirtualAllocEx` ou `WriteProcessMemory` falharem, rode como administrador.
- Feito para a **demo**. Na versão completa os offsets provavelmente mudam, mas
  como tudo vem de AOB com captura, há uma boa chance de continuar resolvendo.

---

## 7. Arquivos

```
Trainer\
  src\trainer.h        estruturas, classes e a tabela de features
  src\memory.cpp       processo, scanner AOB, mini-assembler x64
  src\engine.cpp       assinaturas, code cave, hooks e loop de aplicação
  src\main.cpp         interface Win32 (desenhada a mão, tema escuro)
  src\selftest.cpp     validador de assinaturas e trampolins
  build.bat                        compila os dois executáveis
  ADICIONAR_EXCLUSAO_DEFENDER.bat  exclusão do antivírus (pede UAC)
  INICIAR_TRAINER.bat              atalho para abrir o trainer
```
