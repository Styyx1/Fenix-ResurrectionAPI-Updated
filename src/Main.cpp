#include "Logging.h"
#include "API/ResurrectionAPI.h"

class Subscribers
{
    std::vector<std::unique_ptr<ResurrectionAPI>> data;
    std::mutex subscribers_mutex;

public:
    void add(std::unique_ptr<ResurrectionAPI> api)
    {
        const std::lock_guard<std::mutex> lock(subscribers_mutex);
        data.push_back(std::move(api));
    }
    bool should_resurrect(RE::Actor* a) const
    {
        bool ans = false;
        for (auto& i : data) {
            ans = ans || i->should_resurrect(a);
        }
        return ans;
    }
    void resurrect(RE::Actor* a)
    {
        for (auto& i : data) {
            if (i->should_resurrect(a)) {
                i->resurrect(a);
                return;
            }
        }
    }
} subscribers;

extern "C" DLLEXPORT void ResurrectionAPI_AddSubscriber(std::unique_ptr<ResurrectionAPI> api)
{
    return subscribers.add(std::move(api));
}
//SE: 37534 AE: 38483 
void Character__invalidate_cached(RE::Actor* a, RE::ActorValue av)
{
    using func_t = decltype(Character__invalidate_cached);
    REL::Relocation<func_t> func{ REL::RelocationID(37534, 38483)};
    return func(a, av);
}
void resurrect(RE::Actor* a) {
    return subscribers.resurrect(a);
}
bool should_resurrect(RE::Actor* a) {
    return subscribers.should_resurrect(a);
}
bool should_cancel_dmg(RE::Actor* a, float new_dmg_mod)
{
    float old_dmg_mod = a->GetActorRuntimeData().healthModifiers.modifiers[RE::ACTOR_VALUE_MODIFIERS::kDamage];
    a->GetActorRuntimeData().healthModifiers.modifiers[RE::ACTOR_VALUE_MODIFIERS::kDamage] = new_dmg_mod;
    Character__invalidate_cached(a, RE::ActorValue::kHealth);
    bool ans = a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) <= 0.0f && should_resurrect(a);
    if (ans) {
        resurrect(a);
    }
    a->GetActorRuntimeData().healthModifiers.modifiers[RE::ACTOR_VALUE_MODIFIERS::kDamage] = old_dmg_mod;
    Character__invalidate_cached(a, RE::ActorValue::kHealth);
    return ans;
}
//template from fenix utils
template <size_t BRANCH_TYPE, uint64_t ID, size_t offset = 0, bool call = false>
auto add_trampoline(Xbyak::CodeGenerator* xbyakCode)
{
    constexpr REL::ID funcOffset = REL::ID(ID);
    auto funcAddr = funcOffset.address();
    auto size = xbyakCode->getSize();
    auto& trampoline = SKSE::GetTrampoline();
    auto result = trampoline.allocate(size);
    std::memcpy(result, xbyakCode->getCode(), size);
    if constexpr (!call)
        return trampoline.write_branch<BRANCH_TYPE>(funcAddr + offset, (std::uintptr_t)result);
    else
        return trampoline.write_call<BRANCH_TYPE>(funcAddr + offset, (std::uintptr_t)result);
}
void apply_canceldamage()
{
    // SkyrimSE.exe+62131C // AE: SkyrimSE.exe+6b299a
    uintptr_t ret_cancel = REL::ID(38468).address() + 0x1fa;

    // SkyrimSE.exe+62128A // // AE: SkyrimSE.exe+6b2908
    uintptr_t ret_nocancel = REL::ID(38468).address() + 0x168;
    struct Code : Xbyak::CodeGenerator
    {
        Code(uintptr_t func_addr, uintptr_t ret_nocancel, uintptr_t ret_cancel)
        {
            Xbyak::Label nocancel;
            // rdi  = modifiers
            // rbp  = modifier
            // xmm6 = new_dmg_mod
            // rbx  = a
            // xmm1 <- old_dmg_mod
            cmp(esi, 0x18);
            jnz(nocancel);

            mov(rcx, rbx);
            movss(xmm1, xmm6);
            mov(rax, func_addr);
            movaps(xmm9, xmm0);  // restore xmm0
            call(rax);
            movaps(xmm0, xmm9);  // restore xmm0
            test(al, al);
            jz(nocancel);
            
            mov(rax, ret_cancel);
            jmp(rax);


            L(nocancel);
            movss(xmm1, dword[rdi + rbp * 4]);  // restore xmm1
            mov(rax, ret_nocancel);
            jmp(rax);
        }        
    }
    xbyakCode{ uintptr_t(should_cancel_dmg), ret_nocancel, ret_cancel};    
    add_trampoline<5, 38468, 0x163>(&xbyakCode);  // SkyrimSE.exe+621285 // SkyrimSE.exe+6b2903
    logger::debug("xbyak done");
}
class IsFatalAttackHook : public Singleton<IsFatalAttackHook>
{
public:    

    inline static const REL::Relocation target{ REL::RelocationID(21285, 21744), 0x3b };
    inline static const auto address{ target.address() };

    static void Hook()
    {
        _isFatalAttack = SKSE::GetTrampoline().write_call<5>(address, isFatalAttack);
        logger::info("Installed Fatal Attack hook");
        logger::info("");        
    }
private:
    static bool isFatalAttack(RE::Actor* attacker, RE::Actor* victim) {
        return !should_resurrect(victim) && _isFatalAttack(attacker, victim);
    }
    static inline REL::Relocation<decltype(isFatalAttack)> _isFatalAttack;
};
void apply_hooks()
{
    IsFatalAttackHook::Hook();  // for killmoves
    apply_canceldamage();
}
void Listener(SKSE::MessagingInterface::Message* message) noexcept
{
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        apply_hooks();
    }
}
SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitLogging();

    const auto plugin{ SKSE::PluginDeclaration::GetSingleton() };
    const auto name{ plugin->GetName() };
    const auto version{ plugin->GetVersion() };

    logger::info("{} {} is loading...", name, version);

    Init(skse);
    SKSE::AllocTrampoline(98);

    if (const auto messaging{ SKSE::GetMessagingInterface() }; !messaging->RegisterListener(Listener)) {
        return false;
    }

    logger::info("{} has finished loading.", name);
    logger::info("");

    return true;
}
