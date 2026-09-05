# Trainer - Trails in the Sky the 2nd Chapter (Demo)

Trainer em C++ para `sora_2nd.exe`, com executável próprio e interface com
toggles e sliders, no mesmo estilo do trainer de DragonSword: Awakening.

Diferente daquele (que era Python + padrões vindos do WeMod), aqui **não existia
nenhuma tabela pronta**: as assinaturas, os offsets das structs e os pontos de
hook foram extraídos por engenharia reversa do próprio `sora_2nd.exe`.

---

## 1. Antes de tudo: o Windows Defender

O Defender **apaga o executável** (`Trojan:Win32/Wacatac` / heurística de
HackTool). Isso é falso positivo e acontece com qualquer trainer: o programa
abre o processo do jogo e escreve na memória dele — é literalmente o
comportamento que a heurística procura.

O código-fonte inteiro está em `src\`. Nada é baixado, nada roda em segundo
plano, nada toca em rede.

Passos:

1. Rode **`ADICIONAR_EXCLUSAO_DEFENDER.bat`** (vai pedir UAC).
   Ele adiciona só esta pasta `Trainer\` às exclusões.
2. Rode **`build.bat`** para recompilar (o exe anterior provavelmente já foi
   removido pelo Defender).
3. Rode **`INICIAR_TRAINER.bat`**.

Se preferir fazer à mão: Segurança do Windows > Proteção contra vírus e ameaças
> Gerenciar configurações > Exclusões > Adicionar uma exclusão > Pasta > escolha
esta pasta.

---

## 2. Como usar

1. Abra o jogo normalmente pela Steam.
2. Abra o trainer (`INICIAR_TRAINER.bat`).
3. O selo no canto superior direito deve ficar verde:
   `CONECTADO (PID ...) - 7/7 assinaturas`.
4. **Carregue ou inicie uma partida.** Antes disso a save ainda não existe na
   memória e as opções de HP/EP/CP/dinheiro não têm onde escrever — o rodapé
   avisa `Save ainda nao carregado`.
5. Marque as opções que quiser. Sliders valem enquanto a opção estiver marcada.

Atalhos globais: **Ctrl+F1 a Ctrl+F8** ligam/desligam as opções sem slider
(a tecla aparece à direita de cada opção). Usei Ctrl de propósito para não
roubar as teclas F do jogo.

O botão **"Desativar tudo e restaurar o jogo"** desmarca tudo e remove os
patches. Fechar o trainer faz a mesma coisa automaticamente.

---

## 3. O que cada opção faz

### Aba Combate

| Opção | Como funciona |
|---|---|
| **God Mode** | Hook em `battle::Object::AddHp`. Se o alvo do dano é da sua party, o delta vira 0. Cura continua funcionando normal. |
| **Redutor de Dano Recebido** | Mesmo hook: divide o dano que a party recebe pelo valor do slider. |
| **Multiplicador de Dano Causado** | Mesmo hook: multiplica o dano quando o alvo **não** é da party. |
| **Morte Instantânea** | Mesmo hook: força −999999 de HP em qualquer inimigo atingido. |
| **HP / EP / CP infinito** | Não é hook. O trainer relê a savedata ~40x por segundo e repõe o valor no máximo. Funciona dentro e fora de batalha, e revive quem estiver caído. |

God Mode, redutor, multiplicador e morte instantânea compartilham um único
trampolim. O trainer distingue party de inimigo comparando o ponteiro do
`battle::Status` do alvo com a faixa de memória do array de personagens da
savedata — personagens jogáveis vivem na savedata, monstros são alocados por
batalha.

### Aba Progressão

| Opção | Como funciona |
|---|---|
| **Multiplicador de EXP** | Hook no epílogo da função que calcula a EXP de cada personagem na tela de resultado. O valor em `eax` é multiplicado antes do `ret`, com teto de 999.999.999. |

### Aba Itens & Dinheiro

| Opção | Como funciona |
|---|---|
| **Mira Travada** | Escreve o valor do campo na savedata. Teto do jogo: 9.999.999. |
| **Sepith Travado** | Escreve os 7 tipos (terra, água, fogo, vento, tempo, espaço, miragem). Teto: 99.999. |
| **Itens Infinitos** | Repõe a quantidade dos itens que você **já possui**. Não desbloqueia itens que você nunca pegou — de propósito, para não bagunçar a progressão nem a save. |

### Aba Party

Leitura ao vivo do array de personagens da savedata: slot, ID, nível, HP, EP,
CP e EXP. Serve para conferir que o trainer realmente achou os dados certos
antes de você ligar qualquer coisa. Linha verde = HP cheio, vermelha = caído.

---

## 4. Detalhes técnicos

Tudo abaixo foi obtido estaticamente do `sora_2nd.exe` (x64, motor **fdk** da
Falcom, com RTTI e caminhos de `__FILE__` preservados no binário).

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
    +0x20B64C  mira

battle::Manager
    +0x2448  ponteiro para o array de participantes da batalha
    +0x2450  quantidade
```

### Pontos de hook

| Alvo | RVA nesta build | Bytes roubados |
|---|---|---|
| `battle::Object::AddHp` | `0x0E4900` | 5 |
| epílogo do cálculo de EXP | `0x0F103E` | 10 |

`AddHp` é o funil por onde passa **todo** dano e cura de combate:
`rcx` = objeto de batalha, `edx` = delta, `[rcx+0]` = `battle::Status` do alvo.

### Assinaturas AOB

O trainer não usa endereços fixos: ele varre a seção de código e captura os
offsets direto das instruções (notação `sN` = N bytes capturados).

```
battle::Manager   48 8B 05 s4 48 8B 98 48 24 00 00 8B 80 50 24 00 00 48 8D 3C C3
savedata::Manager 48 8B 05 s4 0F B7 94 88 s4 48 8B CF
array de Status   48 69 C2 s4 48 05 s4 49 03 C2
mira              89 82 s4 45 33 C9
sepith            89 82 s4 44 8B 93 AC 5B 12 00
Object::AddHp     48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 48 8B B9 E0 05 00 00
cálculo de EXP    8B C5 48 8B AC 24 88 00 00 00 48 81 C4 90 00 00 00
```

Todas dão **exatamente 1 resultado** nesta build.

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

Rode `SoraSelfTest.exe ..\sora_2nd.exe`. Ele carrega a seção de código direto
do arquivo e mostra, para cada assinatura, quantos resultados encontrou e os
valores capturados. Também imprime os bytes dos dois trampolins para conferência.

- `hits=1` em tudo: o trainer funciona sem mudança.
- `hits=0` em alguma: aquele recurso específico some (o trainer continua rodando
  com o resto). O selo mostra quantas das 7 assinaturas foram encontradas.
- `hits=2` ou mais: a assinatura ficou ambígua e precisa ser estendida em
  `src\engine.cpp`.

---

## 6. Cuidados

- **HP/EP/CP infinito, mira, sepith e itens escrevem na savedata em memória.**
  Se você salvar o jogo com eles ligados, os valores ficam gravados no save.
  Isso é o esperado num trainer, mas vale saber.
- O trainer só escreve em slots de personagem que passam na validação
  (nível 1–255, HP máximo 1–99999, HP ≤ HP máximo, etc.), para não corromper
  áreas vizinhas da save.
- Se `VirtualAllocEx` ou `WriteProcessMemory` falharem, rode o trainer como
  administrador.
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
