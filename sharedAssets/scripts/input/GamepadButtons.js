// NorthStarCpp gamepad button masks from the input_plugin.h XInput contract.
// These are bit flags, NOT keyboard Virtual-Key codes.
(() => {
    "use strict";
    class GamepadButtons {
        static DPadUp = 0x0001;
        static DPadDown = 0x0002;
        static DPadLeft = 0x0004;
        static DPadRight = 0x0008;
        static Start = 0x0010;
        static Back = 0x0020;
        static LeftThumb = 0x0040;
        static RightThumb = 0x0080;
        static LeftShoulder = 0x0100;
        static RightShoulder = 0x0200;
        static A = 0x1000;
        static B = 0x2000;
        static X = 0x4000;
        static Y = 0x8000;
        static Jump = 0x1000;
        static Interact = 0x4000;
    }
    Object.freeze(GamepadButtons);
    NorthStar.inputTypes.registerGamepadButtons(GamepadButtons);
})();
