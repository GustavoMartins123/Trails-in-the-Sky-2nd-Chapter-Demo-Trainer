// =============================================================================
//  Trails in the Sky the 2nd Chapter (Demo) - Trainer
//  sora_2nd.exe / motor "fdk" da Falcom - x64
//
//  Trainer externo: nao injeta DLL. Le e escreve na memoria do jogo com
//  ReadProcessMemory/WriteProcessMemory e instala trampolins em uma code cave
//  alocada dentro do alcance de um jmp rel32 (+-2GB do modulo).
// =============================================================================
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

// -----------------------------------------------------------------------------
// Processo
// -----------------------------------------------------------------------------
class Process {
public:
    HANDLE   handle = nullptr;
    DWORD    pid    = 0;
    uint64_t base   = 0;   // base do modulo principal
    uint32_t size   = 0;   // SizeOfImage

    bool attach(const wchar_t* exeName);
    void detach();
    bool alive() const;

    bool read (uint64_t addr, void* dst, size_t n) const;
    bool write(uint64_t addr, const void* src, size_t n) const;

    template <typename T> T readT(uint64_t addr, T def = T()) const {
        T v = def; read(addr, &v, sizeof(T)); return v;
    }
    template <typename T> bool writeT(uint64_t addr, const T& v) const {
        return write(addr, &v, sizeof(T));
    }

    // Congela todas as threads do jogo (usado ao aplicar/remover patches).
    std::vector<HANDLE> freezeThreads() const;
    void thawThreads(std::vector<HANDLE>& v) const;
};

// -----------------------------------------------------------------------------
// Scanner AOB
//   Sintaxe do padrao:  "48 8B 05 s4 0F B7 94 88 s4 48 8B CF"
//     XX  = byte literal
//     ??  = curinga
//     sN  = curinga de N bytes cujo valor e capturado (little endian)
// -----------------------------------------------------------------------------
struct Pattern {
    std::vector<int>                 tokens;   // -1 = curinga
    std::vector<std::pair<int, int>> caps;     // (offset, tamanho)
    bool parse(const char* text);
};

class Scanner {
public:
    explicit Scanner(const Process& p) : proc(p) {}
    bool loadImage();                                  // le a secao de codigo
    // Devolve todos os enderecos absolutos que casam. captures recebe os
    // valores capturados do primeiro match.
    std::vector<uint64_t> find(const Pattern& pat,
                               std::vector<uint64_t>* captures = 0) const;
private:
    const Process&        proc;
    std::vector<uint8_t>  img;
    uint64_t              imgBase = 0;
};

// -----------------------------------------------------------------------------
// Montador x64 minimo (labels, rel32 e enderecamento RIP-relativo)
// -----------------------------------------------------------------------------
class Asm {
public:
    explicit Asm(uint64_t baseAddr) : base(baseAddr) {}

    uint64_t here() const { return base + buf.size(); }

    Asm& db(std::initializer_list<int> bytes);
    Asm& raw(const uint8_t* p, size_t n);
    Asm& label(const char* name);

    // prefixo + disp32(target - fim da instrucao) + sufixo
    Asm& rip(std::initializer_list<int> prefix, uint64_t target,
             std::initializer_list<int> suffix = {});
    Asm& jmp (const char* lbl);
    Asm& jmpAbs(uint64_t target);
    Asm& jcc (int cc, const char* lbl);         // cc = segundo byte do 0F 8x
    Asm& cmpVar32(uint64_t varAddr, int imm8);  // cmp dword ptr [rip+var], imm8

    std::vector<uint8_t> finish();

    // condicoes (segundo byte do opcode 0F 8x)
    enum { E = 0x84, NE = 0x85, B = 0x82, AE = 0x83, LE = 0x8E,
           G = 0x8F, L = 0x8C, GE = 0x8D, NS = 0x89, S = 0x88, A = 0x87 };

private:
    uint64_t                                    base;
    std::vector<uint8_t>                        buf;
    std::map<std::string, size_t>               labels;
    std::vector<std::pair<size_t, std::string> > fixups;   // (offset do disp32, label)
};

// -----------------------------------------------------------------------------
// Estado / features
// -----------------------------------------------------------------------------
enum FeatureId {
    F_GODMODE = 0,      // dano recebido zerado                 (hook AddHp)
    F_DEFMUL,           // divisor do dano recebido             (hook AddHp)
    F_DMGMUL,           // multiplicador do dano causado        (hook AddHp)
    F_ONEHIT,           // morte instantanea                    (hook AddHp)

    F_INFHP,            // HP travado no maximo                 (polling)
    F_INFEP,            // EP travado no maximo                 (polling)
    F_INFCP,            // CP travado no maximo                 (polling)

    F_EXPMUL,           // multiplicador de EXP                 (hook CalcExp)
    F_SEPMUL,           // multiplicador de sepith recebido     (hook AddItem)
    F_QZMUL,            // multiplicador do valor elemental dos quartzos (hook)

    F_MIRAMUL,          // multiplicador de mira recebida       (hook AddMira)
    F_ITEMMUL,          // multiplicador de itens recebidos     (hook AddItem)

    F_MIRA,             // mira travada                         (polling)
    F_SEPITH,           // sepith travado                       (polling)
    F_ITEMS,            // itens em estoque travados            (polling)

    F_COUNT
};

// indices no vetor de hooks
enum HookId { HK_ADDHP = 0, HK_EXP, HK_MIRA, HK_ITEM, HK_QUARTZ, HK_COUNT };

struct FeatureState {
    bool  on    = false;
    float value = 1.0f;
};

// Layout de battle::Status (confirmado por engenharia reversa do sora_2nd.exe)
struct StatusView {
    int32_t charaId, level, exp;
    int32_t hp, maxHp, ep, maxEp, cp, maxCp;
};

struct CharaRow {
    int        slot;
    uint64_t   addr;
    StatusView st;
    wchar_t    name[32];
};

// -----------------------------------------------------------------------------
// Engine
// -----------------------------------------------------------------------------
class Engine {
public:
    void start();
    void stop();

    void         setFeature(int id, bool on, float value);
    FeatureState get(int id) const;
    void         disableAll();

    std::wstring statusLine();
    std::wstring detailLine();
    std::vector<CharaRow> partySnapshot();

    // valores editaveis pela GUI
    volatile LONG miraValue   = 9999999;
    volatile LONG sepithValue = 9999;
    volatile LONG itemValue   = 99;
    // a GUI liga isso enquanto a aba Party esta visivel; sem isso o
    // trainer nao le o bloco de Status do jogo a toa
    volatile LONG wantSnapshot = 0;

    bool connected();

private:
    bool  resolve();
    void  releaseAll();

    bool  installHook(int which);
    void  removeHook(int which);
    // Localiza um site de hook. Se ele estiver com um patch orfao (deixado por
    // uma execucao anterior do trainer que morreu sem restaurar), restaura os
    // bytes originais antes de devolver o endereco.
    uint64_t findHookSite(Scanner& sc, const char* pattern, uint32_t steal,
                          std::vector<uint64_t>* caps);
    bool  repairSite(uint64_t site, uint32_t steal, const Pattern& pat);
    void  syncHooks();
    void  writeVars();

    void  poll();
    static DWORD WINAPI threadProc(void* self);

    bool     allocCave();
    uint64_t caveAlloc(uint32_t n);

    // tabela de nomes (datatable "NameTableData")
    void         refreshNameTable();
    const wchar_t* nameFor(int32_t charaId);

    Process  proc;
    HANDLE   thread   = nullptr;
    volatile bool running = false;
    CRITICAL_SECTION lock;
    bool     csReady  = false;

    bool     resolved = false;
    int      missingSigs = 0;
    int      totalSigs   = 0;

    // ---- enderecos resolvidos (absolutos) -----------------------------------
    uint64_t gBattleMgr   = 0;   // ponteiro para battle::Manager
    uint64_t gSaveMgr     = 0;   // ponteiro para savedata::Manager
    uint64_t gDataMgr     = 0;   // ponteiro para datatable::Manager
    uint64_t fnAddHp      = 0;   // battle::Object::AddHp
    uint64_t fnAddMira    = 0;   // savedata::Manager::AddMira
    uint64_t fnAddItem    = 0;   // savedata::Manager::AddItem
    uint64_t sitExp       = 0;   // epilogo do calculo de EXP
    uint64_t sitQuartz    = 0;   // soma dos valores elementais dos quartzos

    uint32_t offStatusArr = 0;   // savedata -> array de Status
    uint32_t offStatusStr = 0;   // stride do Status (0x2A0)
    uint32_t offItems     = 0;   // savedata -> tabela de itens
    uint32_t offMira      = 0;   // savedata -> mira
    uint32_t offSepith    = 0;   // savedata -> sepith[7]

    // layout generico das datatables (vem da assinatura de TableFind)
    uint32_t tblIdxOff = 0x28, tblDirOff = 0x20, tblBaseOff = 0x10;
    uint32_t dirCountOff = 0x4C, dirStrideOff = 0x48, dirStartOff = 0x44;

    // ---- code cave -----------------------------------------------------------
    uint64_t cave = 0, caveNext = 0;
    bool     varsValid = false;   // cache de writeVars (invalido a cada cave nova)
    int      staleFixed = 0;      // quantos patches orfaos foram removidos
    uint64_t vGod = 0, vDefMul = 0, vDmgMul = 0, vOneHit = 0, vExpMul = 0;
    uint64_t vMiraMul = 0, vItemMul = 0, vSepMul = 0, vQzMul = 0;
    uint64_t vPartyLo = 0, vPartyHi = 0;

    struct Hook {
        uint64_t             site  = 0;
        uint32_t             steal = 0;
        uint64_t             tramp = 0;
        std::vector<uint8_t> orig;
        bool                 on    = false;
    };
    Hook hooks[HK_COUNT];

    FeatureState feat[F_COUNT];
    std::vector<CharaRow> snapshot;
    uint64_t partyLo = 0, partyHi = 0;
    int      pollTick = 0;

    // cache da tabela de nomes
    uint64_t nameRecs = 0;
    uint32_t nameStride = 0, nameCount = 0;
    std::vector<uint8_t>            nameBlock;
    std::map<int32_t, std::wstring> nameCache;
};

extern Engine g_engine;

// Descricao declarativa das features, consumida pela GUI.
struct FeatureInfo {
    int            id;
    int            tab;
    const wchar_t* title;
    const wchar_t* desc;
    bool           slider;
    float          lo, hi, def;
    const wchar_t* unit;
};
extern const FeatureInfo kFeatures[];
extern const int         kFeatureCount;
