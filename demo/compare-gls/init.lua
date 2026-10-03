-- Run on godot-luau-script's core VM at startup: make every script in this
-- project a core script, so the bench script's @permissions are honoured
-- (Engine is an INTERNAL class).
LuauInterface.SandboxService:DiscoverCoreScripts()
