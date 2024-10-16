#pragma once

//only here in case i want to add settings or i want to debug stuff. by default, the ini is not even included in the mod
class Settings : public Singleton<Settings>
{
public:
    static void LoadSettings() noexcept;

    inline static bool debug_logging{};
};
