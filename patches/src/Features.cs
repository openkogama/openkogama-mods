namespace OpenKogama.Mods.Patches;

public static class Features
{
    public static readonly (string Flag, string Type, string Method, int? Ret)[] Toggles =
    [
        ("no_build_limit", "ModelingBoxCountConstraint", "CanAddCubeAt", 1),
        ("no_build_limit", "ModelingBoxCountConstraint", "CanRemoveCubeAt", 1),
        ("no_build_limit", "ModelingDynamicBoxConstraint", "CanAddCubeAt", 1),
        ("no_build_limit", "ConstraintVisualizer", "Init", null),
        ("destructibles_unlock", "MVMaterial", "get_IsAvailable", 1),
        ("unlimited_muzzle_position", "Assets.Scripts.WorldObjectTypes.CustomGun.MVCustomGunBlueprint", "SetMuzzlePointPosition", null),

        ("atlas_check_skip", "MaterialLoader", "CheckAtlasIntegrity", 1),

        ("anticheat_off", "CheatHandling", "MachineBanDetected", null),
        ("anticheat_off", "CheatHandling", "CheatSoftwareRunningDetected", null),
        ("anticheat_off", "CheatHandling", "TextureHackDetected", null),
        ("anticheat_off", "CheatHandling", "ExecuteBan", null),

        ("third_person", "PickupItem", "get_FirstPerson", 0),
        ("hide_notifications", "NotificationController", "OnNotificationReceived", null),
        ("hide_notifications", "NotificationsManager", "InstantiateNotification", null),
        ("fast_respawn", "DeathUIController", "StartDeathBriefing", null),
        ("free_elite", "MVClientSettings", "get_IsSubscriber", 1),
        ("antiban", "CheatHandling", "Init", null),
        ("antiban", "MVNetworkGame.OperationRequests", "Ban(CheatType)", null),
        ("antiban", "MVNetworkGame.OperationRequests", "Ban(int,MVPlayer,string)", null),
        ("antiban", "MVNetworkGame.OperationRequests", "Expel", null),
        ("antiban", "MVNetworkGame.OperationRequests", "Kick", null),
        ("railgun_override", "PickupItemRailGun", "DoChargingAnimation", null),
    ];

    public static readonly (string Event, string Type, string Method)[] Hooks =
    [
        ("world.object_added", "WorldNetwork", "AddWorldObject"),
        ("world.object_removed", "MVWorldObjectClientManagerNetwork", "DestroyWO"),
        ("world.object_transferred", "MVGroup", "TransferChild"),
        ("editor.move_character", "MVBuildModeAvatarLocal.EditMode", "MoveCharacter"),
        ("editor.add_prototype", "EditorWorldObjectCreation", "OnAddNewPrototype"),
        ("world.loaded", "WorldNetwork", "HandleDeserializedWorldData"),
        ("loading.start", "LoadingScreenHandler", "Start"),
        ("frame", "MVGameControllerBase", "Update"),
        ("frame", "MVGameController", "Update"),
        ("frame.late", "FullScreenController", "LateUpdate"),
        ("app.quit", "MVGameControllerBase", "OnApplicationQuit"),
        ("app.quit", "MVGameController", "OnApplicationQuit"),
        ("editor.select", "SelectionController", "SelectWO"),
        ("editor.translate_start", "ESTranslate", "Enter"),
        ("editor.translate_end", "ESTranslate", "Exit"),
        ("editor.rotation_reset", "UGUI.Desktop.Scripts.EditMode.Gizmo.RotationHelper", "ResetRotation"),
        ("world.link_added", "MVNetworkGame.OperationRequests", "AddLink"),
        ("world.link_removed", "Links", "RemoveLink"),
        ("world.object_link_added", "MVNetworkGame.OperationRequests", "AddObjectLink"),
        ("world.object_link_removed", "ObjectLinks", "RemoveObjectLink"),
        ("editor.paint_cubes", "PaintCubes", "Execute"),
        ("editor.translate", "ESTranslate", "Execute"),
        ("editor.grid_point", "MVWorldObjectClient", "GetClosestGridPoint"),
        ("editor.rotate_step", "UGUI.Desktop.Scripts.EditMode.Gizmo.RotationHelper", "RotateStep"),
        ("camera.set", "MVCameraController", "SetCamera(CameraType)"),
        ("camera.blue_mode", "MainCameraManager", "set_BlueModeEnabled"),
        ("settings.slider_float", "SettingsSlider", "Initialize(string,float,float,float)"),
        ("settings.slider_int", "SettingsSlider", "Initialize(string,int,int,int)"),
        ("settings.slider_changed", "SettingsSlider", "ValueChanged"),
        ("chat.command", "TextCommand", "Resolve"),
        ("editor.select_hit", "SelectionController", "Select(VoxelHit,bool,bool)"),
        ("editor.deselect_all", "SelectionController", "DeSelectAll"),
        ("editor.deselect_except", "SelectionController", "DeSelectAllExcept"),
        ("editor.deselect_wo", "SelectionController", "DeSelectWorldObject"),
        ("editor.link_loop", "MVWorldObject.dll!LogicObjectManager", "ValidateLink/4"),
        ("editor.link_validate", "MVWorldObject.dll!LogicObjectManager", "ValidateLink/5"),
        ("editor.object_link_enabler", "MVObjectEnabler", "ValidateObjectLinkTarget"),
        ("editor.object_link_transparency", "WorldObjectTypes.MVObjectTransparency.MVObjectTransparency", "ValidateObjectLinkTarget"),
        ("editor.add_object_link", "ESAddObjectLink", "Enter"),
        ("editor.push_state", "FSMEntity", "PushState(EditorEvent,EditorEvent)"),
        ("pvp.killed", "AvatarInteractable", "DoKilledNotification"),
        ("pvp.fell", "AvatarInteractable", "DieFromFalling"),
        ("pvp.stuck", "AvatarInteractable", "DieFromStuck"),
        ("pvp.stuck_long", "AvatarInteractable", "DieFromBeingStuck"),
        ("pvp.suicide", "MVAvatarLocal", "Suicide"),
    ];

    public static readonly (string Flag, string Type, string Method, string Param, string Arg, double Fallback)[] Floats =
    [
        ("model_scale", "EditorWorldObjectCreation", "OnAddNewPrototype", "model_scale_value", "scale", 1.0),
        ("custom_grid", "MVWorldObjectClient", "GetClosestGridPoint", "custom_grid_size", "gridSize", 1.0),
    ];
}
