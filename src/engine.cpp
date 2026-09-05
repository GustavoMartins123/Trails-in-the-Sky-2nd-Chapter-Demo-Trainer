// =============================================================================
//  engine.cpp - resolucao de enderecos, hooks e loop de aplicacao
//
//  Tudo o que esta aqui saiu de engenharia reversa estatica do sora_2nd.exe
//  (demo de Trails in the Sky the 2nd Chapter, motor fdk / Falcom, x64):
//
//    battle::Status  (0x2A0 bytes)
//        +0x00 charaId   +0x04 level   +0x08 exp
//        +0x0C hp        +0x10 maxHp
//        +0x14 ep        +0x18 maxEp
//        +0x1C cp        +0x20 maxCp
//
//    savedata::Manager
//        +offStatusArr  array de Status (slots 1..99, stride 0x2A0)
//        +offItems      tabela de itens: 5000 registros de 4 bytes
//                       (u16 quantidade, u8 flags, u8 -)
//        +offMira       mira (dinheiro), teto 9.999.999
//        +offSepith     sepith[7] (terra/agua/fogo/vento/tempo/espaco/miragem)
//
//    battle::Manager
//        +0x2448 ponteiro para o array de participantes da batalha
//        +0x2450 quantidade
//
//    battle::Object::AddHp(this, delta, ...)  <- ponto unico por onde passa
//        todo dano e cura em combate. [this+0] e o battle::Status do alvo.
// =============================================================================
#include "trainer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

Engine g_engine;

// -----------------------------------------------------------------------------
// Assinaturas
// -----------------------------------------------------------------------------
namespace sig {
// mov rax,[rip+battle::Manager] ; mov rbx,[rax+2448] ; mov eax,[rax+2450]
const char* BATTLE_MGR =
    "48 8B 05 s4 48 8B 98 48 24 00 00 8B 80 50 24 00 00 48 8D 3C C3";
// mov rax,[rip+savedata::Manager] ; movzx edx,word[rax+rcx*4+itemTable]
const char* SAVE_MGR =
    "48 8B 05 s4 0F B7 94 88 s4 48 8B CF";
// imul rax,rdx,0x2A0 ; add rax,statusArray ; add rax,r10
const char* STATUS_ARR =
    "48 69 C2 s4 48 05 s4 49 03 C2";
// mov [rdx+mira],eax
const char* MIRA =
    "89 82 s4 45 33 C9";
// mov [rdx+sepith],eax
const char* SEPITH =
    "89 82 s4 44 8B 93 AC 5B 12 00";
// prologo de battle::Object::AddHp
const char* ADDHP =
    "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 48 8B B9 E0 05 00 00";
// epilogo do calculo de EXP da tela de resultado (retorna a EXP em eax)
const char* EXPCALC =
    "8B C5 48 8B AC 24 88 00 00 00 48 81 C4 90 00 00 00";
}

static const int   kStatusSlots = 100;
static const int   kItemCount   = 5000;
static const DWORD kPollMs      = 25;

// -----------------------------------------------------------------------------
// Tabela de features consumida pela GUI
// -----------------------------------------------------------------------------
const FeatureInfo kFeatures[] = {
 { F_GODMODE, 0, L"God Mode (imune a dano)",
   L"Zera todo o dano recebido pelos personagens da sua party.",
   false, 0, 0, 0, L"" },
 { F_DEFMUL, 0, L"Redutor de Dano Recebido",
   L"Divide o dano que a sua party recebe. 5x = voce leva 1/5 do dano.",
   true, 1.0f, 50.0f, 3.0f, L"x" },
 { F_DMGMUL, 0, L"Multiplicador de Dano Causado",
   L"Multiplica o dano que voce aplica nos inimigos.",
   true, 1.0f, 100.0f, 5.0f, L"x" },
 { F_ONEHIT, 0, L"Morte Instantanea (One Hit Kill)",
   L"Qualquer golpe seu tira 999999 de HP do inimigo.",
   false, 0, 0, 0, L"" },
 { F_INFHP, 0, L"HP Infinito (trava no maximo)",
   L"Mantem o HP de toda a party cheio, dentro e fora de batalha.",
   false, 0, 0, 0, L"" },
 { F_INFEP, 0, L"EP Infinito",
   L"Mantem o EP cheio: artes nao consomem nada na pratica.",
   false, 0, 0, 0, L"" },
 { F_INFCP, 0, L"CP Maximo",
   L"Mantem o CP no maximo: S-Crafts sempre disponiveis.",
   false, 0, 0, 0, L"" },

 { F_EXPMUL, 1, L"Multiplicador de EXP",
   L"Multiplica a experiencia recebida no fim de cada batalha.",
   true, 1.0f, 100.0f, 5.0f, L"x" },

 { F_MIRA, 2, L"Mira Travada",
   L"Trava o seu dinheiro no valor definido abaixo (teto do jogo: 9.999.999).",
   false, 0, 0, 0, L"" },
 { F_SEPITH, 2, L"Sepith Travado",
   L"Trava os 7 tipos de sepith no valor definido abaixo (teto: 99.999).",
   false, 0, 0, 0, L"" },
 { F_ITEMS, 2, L"Itens Infinitos",
   L"Repoe a quantidade dos itens que voce ja possui. Nao desbloqueia itens novos.",
   false, 0, 0, 0, L"" },
};
const int kFeatureCount = (int)(sizeof(kFeatures) / sizeof(kFeatures[0]));

// -----------------------------------------------------------------------------
// Ciclo de vida
// -----------------------------------------------------------------------------
void Engine::start()
{
    if (!csReady) { InitializeCriticalSection(&lock); csReady = true; }
    for (int i = 0; i < kFeatureCount; ++i)
        feat[kFeatures[i].id].value = kFeatures[i].slider ? kFeatures[i].def : 0.0f;
    running = true;
    thread  = CreateThread(nullptr, 0, &Engine::threadProc, this, 0, nullptr);
}

void Engine::stop()
{
    running = false;
    if (thread) { WaitForSingleObject(thread, 3000); CloseHandle(thread); thread = nullptr; }
    if (csReady) {
        EnterCriticalSection(&lock);
        disableAll();
        releaseAll();
        LeaveCriticalSection(&lock);
        DeleteCriticalSection(&lock);
        csReady = false;
    }
}

bool Engine::connected()
{
    if (!csReady) return false;
    EnterCriticalSection(&lock);
    bool r = (proc.handle != nullptr) && resolved;
    LeaveCriticalSection(&lock);
    return r;
}

void Engine::setFeature(int id, bool on, float value)
{
    if (id < 0 || id >= F_COUNT || !csReady) return;
    EnterCriticalSection(&lock);
    feat[id].on = on;
    feat[id].value = value;
    LeaveCriticalSection(&lock);
}

FeatureState Engine::get(int id) const
{
    if (id < 0 || id >= F_COUNT) return FeatureState();
    return feat[id];
}

void Engine::disableAll()
{
    for (int i = 0; i < F_COUNT; ++i) feat[i].on = false;
    writeVars();
    syncHooks();
}

// -----------------------------------------------------------------------------
// Resolucao de enderecos
// -----------------------------------------------------------------------------
static uint64_t ripTarget(uint64_t site, uint64_t disp32, int insnLen)
{
    return site + insnLen + (int64_t)(int32_t)(uint32_t)disp32;
}

bool Engine::resolve()
{
    Scanner sc(proc);
    if (!sc.loadImage()) return false;

    missingSigs = 0;
    Pattern p;
    std::vector<uint64_t> caps;
    std::vector<uint64_t> hits;

    // battle::Manager  ------------------------------------------------------
    if (p.parse(sig::BATTLE_MGR)) {
        hits = sc.find(p, &caps);
        if (hits.size() == 1 && caps.size() == 1)
            gBattleMgr = ripTarget(hits[0], caps[0], 7);
        else ++missingSigs;
    }
    // savedata::Manager + tabela de itens ------------------------------------
    if (p.parse(sig::SAVE_MGR)) {
        hits = sc.find(p, &caps);
        if (hits.size() == 1 && caps.size() == 2) {
            gSaveMgr = ripTarget(hits[0], caps[0], 7);
            offItems = (uint32_t)caps[1];
        } else ++missingSigs;
    }
    // array de Status na savedata --------------------------------------------
    if (p.parse(sig::STATUS_ARR)) {
        hits = sc.find(p, &caps);
        if (hits.size() == 1 && caps.size() == 2) {
            offStatusStr = (uint32_t)caps[0];
            offStatusArr = (uint32_t)caps[1];
        } else ++missingSigs;
    }
    // mira --------------------------------------------------------------------
    if (p.parse(sig::MIRA)) {
        hits = sc.find(p, &caps);
        if (hits.size() == 1 && caps.size() == 1) offMira = (uint32_t)caps[0];
        else ++missingSigs;
    }
    // sepith ------------------------------------------------------------------
    if (p.parse(sig::SEPITH)) {
        hits = sc.find(p, &caps);
        if (hits.size() == 1 && caps.size() == 1) offSepith = (uint32_t)caps[0];
        else ++missingSigs;
    }
    // battle::Object::AddHp ---------------------------------------------------
    if (p.parse(sig::ADDHP)) {
        hits = sc.find(p, nullptr);
        if (hits.size() == 1) fnAddHp = hits[0];
        else ++missingSigs;
    }
    // epilogo do calculo de EXP ------------------------------------------------
    if (p.parse(sig::EXPCALC)) {
        hits = sc.find(p, nullptr);
        if (hits.size() == 1) sitExp = hits[0];
        else ++missingSigs;
    }

    if (!gSaveMgr || !offStatusArr || !offStatusStr) return false;

    hooks[0].site = fnAddHp; hooks[0].steal = 5;
    hooks[1].site = sitExp;  hooks[1].steal = 10;

    if (!allocCave()) return false;
    return true;
}

void Engine::releaseAll()
{
    for (int i = 0; i < 2; ++i) removeHook(i);
    if (proc.handle && cave)
        VirtualFreeEx(proc.handle, (LPVOID)cave, 0, MEM_RELEASE);
    cave = caveNext = 0;
    resolved = false;
    gBattleMgr = gSaveMgr = fnAddHp = sitExp = 0;
    offStatusArr = offStatusStr = offItems = offMira = offSepith = 0;
    partyLo = partyHi = 0;
    snapshot.clear();
    proc.detach();
}

// -----------------------------------------------------------------------------
// Code cave
// -----------------------------------------------------------------------------
bool Engine::allocCave()
{
    const SIZE_T sz = 0x20000;
    for (int step = 1; step < 0x4000; ++step) {
        uint64_t hint = proc.base - (uint64_t)step * 0x10000;
        if (hint <= 0x100000) break;
        LPVOID p = VirtualAllocEx(proc.handle, (LPVOID)hint, sz,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) { cave = (uint64_t)p; break; }
    }
    if (!cave) {
        LPVOID p = VirtualAllocEx(proc.handle, nullptr, sz,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!p) return false;
        cave = (uint64_t)p;
    }

    uint8_t zero[0x100];
    memset(zero, 0, sizeof(zero));
    proc.write(cave, zero, sizeof(zero));

    vPartyLo = cave + 0x00;
    vPartyHi = cave + 0x08;
    vGod     = cave + 0x10;
    vOneHit  = cave + 0x14;
    vDefMul  = cave + 0x18;
    vDmgMul  = cave + 0x1C;
    vExpMul  = cave + 0x20;
    caveNext = cave + 0x1000;
    return true;
}

uint64_t Engine::caveAlloc(uint32_t n)
{
    uint64_t a = (caveNext + 15) & ~(uint64_t)15;
    caveNext = a + n;
    return a;
}

// -----------------------------------------------------------------------------
// Hooks
// -----------------------------------------------------------------------------
bool Engine::installHook(int which)
{
    Hook& h = hooks[which];
    if (h.on || !h.site || !cave) return false;

    if (h.orig.empty()) {
        h.orig.assign(h.steal, 0);
        if (!proc.read(h.site, &h.orig[0], h.steal)) { h.orig.clear(); return false; }
    }
    if (!h.tramp) {
        uint64_t t = caveAlloc(0x200);
        if (llabs((int64_t)t - (int64_t)(h.site + 5)) > 0x7FF00000LL) return false;

        Asm a(t);
        if (which == 0) {
            // battle::Object::AddHp(this=rcx, delta=edx, ...)
            a.db({0x85, 0xD2});                          // test edx,edx
            a.jcc(Asm::NS, "orig");                      // cura -> nao mexe
            a.db({0x50});                                // push rax
            a.db({0x41, 0x52});                          // push r10
            a.rip({0x4C, 0x8B, 0x15}, vPartyLo);         // mov r10,[party_lo]
            a.db({0x4D, 0x85, 0xD2});                    // test r10,r10
            a.jcc(Asm::E, "done");
            a.db({0x48, 0x8B, 0x01});                    // mov rax,[rcx]  (Status*)
            a.db({0x49, 0x3B, 0xC2});                    // cmp rax,r10
            a.jcc(Asm::B, "enemy");
            a.rip({0x4C, 0x8B, 0x15}, vPartyHi);         // mov r10,[party_hi]
            a.db({0x49, 0x3B, 0xC2});                    // cmp rax,r10
            a.jcc(Asm::AE, "enemy");
            // ---- alvo e um personagem da party
            a.cmpVar32(vGod, 1);
            a.jcc(Asm::NE, "pdef");
            a.db({0x31, 0xD2});                          // xor edx,edx
            a.jmp("done");
            a.label("pdef");
            a.cmpVar32(vDefMul, 0);
            a.jcc(Asm::LE, "done");
            a.db({0xF3, 0x0F, 0x2A, 0xEA});              // cvtsi2ss xmm5,edx
            a.rip({0xF3, 0x0F, 0x5E, 0x2D}, vDefMul);    // divss   xmm5,[defmul]
            a.db({0xF3, 0x0F, 0x2C, 0xD5});              // cvttss2si edx,xmm5
            a.jmp("done");
            // ---- alvo e inimigo
            a.label("enemy");
            a.cmpVar32(vOneHit, 1);
            a.jcc(Asm::NE, "emul");
            a.db({0xBA, 0xC1, 0xBD, 0xF0, 0xFF});        // mov edx,-999999
            a.jmp("done");
            a.label("emul");
            a.cmpVar32(vDmgMul, 0);
            a.jcc(Asm::LE, "done");
            a.db({0xF3, 0x0F, 0x2A, 0xEA});              // cvtsi2ss xmm5,edx
            a.rip({0xF3, 0x0F, 0x59, 0x2D}, vDmgMul);    // mulss   xmm5,[dmgmul]
            a.db({0xF3, 0x0F, 0x2C, 0xD5});              // cvttss2si edx,xmm5
            a.label("done");
            a.db({0x41, 0x5A});                          // pop r10
            a.db({0x58});                                // pop rax
            a.label("orig");
            a.raw(&h.orig[0], h.orig.size());            // mov [rsp+18],rbx
            a.jmpAbs(h.site + h.steal);
        } else {
            // epilogo do calculo de EXP: eax = ebp
            a.db({0x8B, 0xC5});                          // mov eax,ebp
            a.cmpVar32(vExpMul, 0);
            a.jcc(Asm::LE, "done");
            a.db({0xF3, 0x0F, 0x2A, 0xE8});              // cvtsi2ss xmm5,eax
            a.rip({0xF3, 0x0F, 0x59, 0x2D}, vExpMul);    // mulss   xmm5,[expmul]
            a.db({0xF3, 0x0F, 0x2C, 0xC5});              // cvttss2si eax,xmm5
            a.db({0x85, 0xC0});                          // test eax,eax
            a.jcc(Asm::NS, "done");
            a.db({0xB8, 0xFF, 0xC9, 0x9A, 0x3B});        // overflow -> 999999999
            a.label("done");
            a.raw(&h.orig[2], h.orig.size() - 2);        // mov rbp,[rsp+88]
            a.jmpAbs(h.site + h.steal);
        }

        std::vector<uint8_t> code = a.finish();
        if (!proc.write(t, &code[0], code.size())) return false;
        h.tramp = t;
    }

    std::vector<uint8_t> patch(h.steal, 0x90);
    patch[0] = 0xE9;
    int32_t rel = (int32_t)((int64_t)h.tramp - (int64_t)(h.site + 5));
    memcpy(&patch[1], &rel, 4);

    std::vector<HANDLE> frozen = proc.freezeThreads();
    bool ok = proc.write(h.site, &patch[0], patch.size());
    proc.thawThreads(frozen);

    h.on = ok;
    return ok;
}

void Engine::removeHook(int which)
{
    Hook& h = hooks[which];
    if (!h.on || h.orig.empty()) { h.on = false; return; }
    std::vector<HANDLE> frozen = proc.freezeThreads();
    proc.write(h.site, &h.orig[0], h.orig.size());
    proc.thawThreads(frozen);
    h.on = false;
}

void Engine::syncHooks()
{
    bool wantHp  = feat[F_GODMODE].on || feat[F_DEFMUL].on ||
                   feat[F_DMGMUL].on  || feat[F_ONEHIT].on;
    bool wantExp = feat[F_EXPMUL].on;

    if (wantHp && !hooks[0].on)  installHook(0);
    if (!wantHp && hooks[0].on)  removeHook(0);
    if (wantExp && !hooks[1].on) installHook(1);
    if (!wantExp && hooks[1].on) removeHook(1);
}

void Engine::writeVars()
{
    if (!cave || !proc.handle) return;

    int32_t god    = feat[F_GODMODE].on ? 1 : 0;
    int32_t onehit = feat[F_ONEHIT].on  ? 1 : 0;
    float   defmul = feat[F_DEFMUL].on ? feat[F_DEFMUL].value : 0.0f;
    float   dmgmul = feat[F_DMGMUL].on ? feat[F_DMGMUL].value : 0.0f;
    float   expmul = feat[F_EXPMUL].on ? feat[F_EXPMUL].value : 0.0f;

    proc.writeT(vGod,    god);
    proc.writeT(vOneHit, onehit);
    proc.writeT(vDefMul, defmul);
    proc.writeT(vDmgMul, dmgmul);
    proc.writeT(vExpMul, expmul);
    proc.writeT(vPartyLo, partyLo);
    proc.writeT(vPartyHi, partyHi);
}

// -----------------------------------------------------------------------------
// Loop
// -----------------------------------------------------------------------------
DWORD WINAPI Engine::threadProc(void* self)
{
    Engine* e = (Engine*)self;
    while (e->running) {
        EnterCriticalSection(&e->lock);
        e->poll();
        LeaveCriticalSection(&e->lock);
        Sleep(kPollMs);
    }
    return 0;
}

static bool statusLooksValid(const StatusView& s)
{
    if (s.level < 1 || s.level > 255)            return false;
    if (s.maxHp < 1 || s.maxHp > 99999)          return false;
    if (s.maxEp < 0 || s.maxEp > 99999)          return false;
    if (s.maxCp < 0 || s.maxCp > 99999)          return false;
    if (s.hp < 0 || s.hp > s.maxHp)              return false;
    if (s.ep < 0 || s.ep > s.maxEp)              return false;
    if (s.cp < 0 || s.cp > s.maxCp)              return false;
    if (s.charaId < 0 || s.charaId > 0xFFFF)     return false;
    return true;
}

void Engine::poll()
{
    // --- conexao --------------------------------------------------------------
    if (proc.handle && !proc.alive()) releaseAll();
    if (!proc.handle) {
        snapshot.clear();
        if (!proc.attach(L"sora_2nd.exe")) return;
        resolved = resolve();
        if (!resolved) { proc.detach(); return; }
    }
    if (!resolved) return;

    // --- ponteiros dinamicos --------------------------------------------------
    uint64_t sav = proc.readT<uint64_t>(gSaveMgr, 0);
    if (sav > 0x10000) {
        partyLo = sav + offStatusArr;
        partyHi = partyLo + (uint64_t)kStatusSlots * offStatusStr;
    } else {
        partyLo = partyHi = 0;
    }

    writeVars();
    syncHooks();

    if (!partyLo) { snapshot.clear(); return; }

    // --- array de Status ------------------------------------------------------
    const size_t blockSize = (size_t)kStatusSlots * offStatusStr;
    if (blockSize < sizeof(StatusView)) { snapshot.clear(); return; }
    static std::vector<uint8_t> block;
    if (block.size() != blockSize) block.assign(blockSize, 0);
    if (!proc.read(partyLo, &block[0], blockSize)) { snapshot.clear(); return; }

    std::vector<CharaRow> rows;
    bool infHp = feat[F_INFHP].on, infEp = feat[F_INFEP].on, infCp = feat[F_INFCP].on;

    for (int slot = 1; slot < kStatusSlots; ++slot) {
        const uint8_t* rec = &block[(size_t)slot * offStatusStr];
        StatusView s;
        memcpy(&s, rec, sizeof(StatusView));
        if (!statusLooksValid(s)) continue;

        CharaRow r; r.slot = slot; r.addr = partyLo + (uint64_t)slot * offStatusStr; r.st = s;
        rows.push_back(r);

        if (infHp && s.hp != s.maxHp) proc.writeT<int32_t>(r.addr + 0x0C, s.maxHp);
        if (infEp && s.ep != s.maxEp) proc.writeT<int32_t>(r.addr + 0x14, s.maxEp);
        if (infCp && s.cp != s.maxCp) proc.writeT<int32_t>(r.addr + 0x1C, s.maxCp);
    }
    snapshot.swap(rows);

    // --- economia -------------------------------------------------------------
    if (feat[F_MIRA].on && offMira) {
        int32_t want = (int32_t)miraValue;
        if (want < 0) want = 0;
        if (want > 9999999) want = 9999999;
        if (proc.readT<int32_t>(sav + offMira, -1) != want)
            proc.writeT<int32_t>(sav + offMira, want);
    }
    if (feat[F_SEPITH].on && offSepith) {
        int32_t want = (int32_t)sepithValue;
        if (want < 0) want = 0;
        if (want > 99999) want = 99999;
        int32_t cur[7] = {0};
        proc.read(sav + offSepith, cur, sizeof(cur));
        for (int i = 0; i < 7; ++i)
            if (cur[i] != want) proc.writeT<int32_t>(sav + offSepith + i * 4, want);
    }

    // itens: varredura mais espacada, a tabela tem 20 KB
    if (feat[F_ITEMS].on && offItems && (pollTick % 8) == 0) {
        static std::vector<uint8_t> items;
        if (items.size() != (size_t)kItemCount * 4) items.assign((size_t)kItemCount * 4, 0);
        if (proc.read(sav + offItems, &items[0], items.size())) {
            uint16_t want = (uint16_t)itemValue;
            if (want > 999) want = 999;
            for (int i = 0; i < kItemCount; ++i) {
                uint16_t n = *(uint16_t*)&items[(size_t)i * 4];
                if (n > 0 && n < want)
                    proc.writeT<uint16_t>(sav + offItems + (uint32_t)i * 4, want);
            }
        }
    }
    ++pollTick;
}

// -----------------------------------------------------------------------------
// Relatorios para a GUI
// -----------------------------------------------------------------------------
std::wstring Engine::statusLine()
{
    if (!csReady) return L"INICIANDO...";
    EnterCriticalSection(&lock);
    std::wstring out;
    wchar_t buf[256];
    if (!proc.handle)      out = L"AGUARDANDO sora_2nd.exe...";
    else if (!resolved)    out = L"CONECTADO - FALHA AO LOCALIZAR AS ASSINATURAS";
    else {
        int hooksOn = (hooks[0].on ? 1 : 0) + (hooks[1].on ? 1 : 0);
        swprintf(buf, 256, L"CONECTADO  PID %lu  -  %d/7 assinaturas  -  %d hook(s)",
                 (unsigned long)proc.pid, 7 - missingSigs, hooksOn);
        out = buf;
    }
    LeaveCriticalSection(&lock);
    return out;
}

std::wstring Engine::detailLine()
{
    if (!csReady) return L"";
    EnterCriticalSection(&lock);
    std::wstring out;
    wchar_t buf[256];
    if (resolved) {
        if (!partyLo) out = L"Save ainda nao carregado - entre em uma partida.";
        else {
            swprintf(buf, 256, L"savedata=%016llX  status[%d]  %u personagens ativos",
                     (unsigned long long)partyLo, kStatusSlots, (unsigned)snapshot.size());
            out = buf;
        }
    }
    LeaveCriticalSection(&lock);
    return out;
}

std::vector<CharaRow> Engine::partySnapshot()
{
    std::vector<CharaRow> out;
    if (!csReady) return out;
    EnterCriticalSection(&lock);
    out = snapshot;
    LeaveCriticalSection(&lock);
    return out;
}
