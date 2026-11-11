# Applied to the downloaded JUCE sources (see CMakeLists.txt). Safe to run twice.
#
# Audacity's VST3 host on Linux has a faulty IRunLoop: a file descriptor watch
# is removed after it has fired once, and unregistering leaves stale watches
# behind. A JUCE plugin relying on it stops receiving events (the editor stays
# black) and the host can crash when the editor closes. Audacity's run loop
# timers do work, so in Audacity this patch drives JUCE's event queue from a
# host timer instead of from file descriptor callbacks.

set(file "modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
file(READ "${file}" content)

string(FIND "${content}" "PlumppuVoxPollTimer" already)

if(already EQUAL -1)
    function(patch_once anchor replacement)
        string(FIND "${content}" "${anchor}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "JUCE has changed: cannot apply the Audacity run loop patch (${anchor})")
        endif()
        string(REPLACE "${anchor}" "${replacement}" content "${content}")
        set(content "${content}" PARENT_SCOPE)
    endfunction()

    set(anchor "    void registerHandlerForRunLoop (Linux::IRunLoop* l)")
    patch_once("${anchor}" [==[
    static bool hostIsAudacity()
    {
        static const bool result = File::getSpecialLocation (File::hostApplicationPath)
                                       .getFileName().containsIgnoreCase ("audacity");
        return result;
    }

    struct PlumppuVoxPollTimer final : public Linux::ITimerHandler
    {
        static PlumppuVoxPollTimer& get() { static PlumppuVoxPollTimer timer; return timer; }

        void PLUGIN_API onTimer() override
        {
            if (! MessageManager::getInstance()->isThisTheMessageThread())
                return;

            for (int i = 0; i < 256 && detail::dispatchNextMessageOnSystemQueue (true); ++i) {}
        }

        tresult PLUGIN_API queryInterface (const TUID, void** obj) override { *obj = nullptr; return kNoInterface; }
        uint32 PLUGIN_API addRef() override   { return 1; }
        uint32 PLUGIN_API release() override  { return 1; }
    };

    void registerHandlerForRunLoop (Linux::IRunLoop* l)]==])

    set(anchor "            for (auto& fd : LinuxEventLoopInternal::getRegisteredFds())")
    patch_once("${anchor}" [==[
            if (hostIsAudacity())
            {
                loop->registerTimer (&PlumppuVoxPollTimer::get(), 15);
                return;
            }

            for (auto& fd : LinuxEventLoopInternal::getRegisteredFds())]==])

    set(anchor "            loop->unregisterEventHandler (handler);")
    patch_once("${anchor}" [==[
            if (hostIsAudacity())
            {
                loop->unregisterTimer (&PlumppuVoxPollTimer::get());
                return;
            }

            loop->unregisterEventHandler (handler);]==])

    file(WRITE "${file}" "${content}")
endif()
