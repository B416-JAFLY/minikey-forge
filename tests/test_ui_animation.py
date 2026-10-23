import ctypes as C
from pathlib import Path
from test_protocol import lib, call, make, assertion, register
from test_pin_resident_gc import client, PinError
import pytest

lib.host_ui_state.argtypes = []
lib.host_ui_state.restype = C.c_uint
lib.host_ui_pixel.argtypes = [C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_int]
lib.host_ui_pixel.restype = C.c_uint16

BG = 0x0843

def frame_hero(state, age, touch_down=0):
    return [
        [lib.host_ui_pixel(8 + x, 8 + y, state, age, touch_down) for x in range(64)]
        for y in range(64)
    ]

def test_boot_connected_intro_and_ambient_idle_animations():
    # Early boot sweep (150ms) vs completed sweep + shockwave (550ms) vs ambient idle (1400ms)
    f_early = frame_hero(0, 150)
    f_locked = frame_hero(0, 550)
    f_idle1 = frame_hero(0, 1400)
    f_idle2 = frame_hero(0, 1800)

    assert f_early != f_locked, "Boot intro ring sweep and padlock shackle must animate"
    # At 150ms shackle is lifted (y=23..26 has shackle pixels); at 550ms shackle is lowered
    assert lib.host_ui_pixel(8 + 32, 8 + 18, 0, 150, 0) != lib.host_ui_pixel(8 + 32, 8 + 18, 0, 550, 0)
    # Shockwave halo outside main ring (r=26px -> x=32, y=6) is active at 550ms and gone in idle
    assert lib.host_ui_pixel(8 + 32, 8 + 5, 0, 580, 0) != BG
    assert lib.host_ui_pixel(8 + 32, 8 + 5, 0, 1400, 0) == BG
    # Ambient idle continues breathing and orbiting comet
    assert f_idle1 != f_idle2
    # Right status card progress bar fills during boot intro
    assert lib.host_ui_pixel(84 + 4, 58, 0, 100, 0) != lib.host_ui_pixel(84 + 50, 58, 0, 100, 0)
    assert lib.host_ui_pixel(84 + 50, 58, 0, 800, 0) == 0x5E5F

def test_touch_prompt_sonar_and_live_press_feedback():
    f_t1 = frame_hero(1, 100, 0)
    f_t2 = frame_hero(1, 260, 0)
    f_pressed = frame_hero(1, 260, 1)

    assert f_t1 != f_t2, "Touch prompt orbiting comet and sonar ripples must animate"
    assert f_t2 != f_pressed, "Live capacitive touch press must change visual feedback"
    # Pill badge changes from TOUCH (blue 0x0C3F) to HOLD (emerald 0x13A6) when finger presses
    assert lib.host_ui_pixel(88, 32, 1, 260, 0) == 0x0C3F
    assert lib.host_ui_pixel(88, 32, 1, 260, 1) == 0x13A6

def test_apple_pay_faceid_success_animation_phases():
    # Phase 1: badge pop + shockwave (120ms, before second checkmark leg)
    # Phase 2: checkmark stroke 1 drawn (200ms)
    # Phase 3: checkmark stroke 2 with overshoot (400ms)
    # Phase 4: celebration sparkles + diagonal sheen (600ms)
    f_120 = frame_hero(3, 60)
    f_200 = frame_hero(3, 200)
    f_400 = frame_hero(3, 400)
    f_600 = frame_hero(3, 600)

    assert f_120 != f_200 != f_400 != f_600
    # Checkmark vertex (28, 41) becomes crisp white (0xFFFF) once stroke 1 completes
    assert lib.host_ui_pixel(8 + 28, 8 + 41, 3, 50, 0) != 0xFFFF
    assert lib.host_ui_pixel(8 + 28, 8 + 41, 3, 240, 0) == 0xFFFF
    # Checkmark tip (45, 23) becomes crisp white once stroke 2 completes
    assert lib.host_ui_pixel(8 + 45, 8 + 23, 3, 200, 0) != 0xFFFF
    assert lib.host_ui_pixel(8 + 45, 8 + 23, 3, 520, 0) == 0xFFFF
    # Celebration corner sparkle at (10, 12) lights up around 600ms and is absent at 100ms
    assert lib.host_ui_pixel(8 + 10, 8 + 12, 3, 600, 0) != BG
    assert lib.host_ui_pixel(8 + 10, 8 + 12, 3, 100, 0) == BG

def test_denied_shake_and_progressive_cross():
    # Horizontal shake shifts the circle at age=40ms (+5px) vs age=100ms (-5px) vs settled age=500ms (0px)
    f_shake_pos = frame_hero(4, 40)
    f_shake_neg = frame_hero(4, 100)
    f_settled = frame_hero(4, 500)
    assert f_shake_pos != f_shake_neg != f_settled
    # Center of X cross (32, 32) is white after both strokes complete
    assert lib.host_ui_pixel(8 + 32, 8 + 32, 4, 300, 0) == 0xFFFF

def test_ctap_protocol_drives_ui_states_correctly():
    lib.host_init()
    assert lib.host_ui_state() == 0

    # Registration with touch -> state 3 (PASSED)
    cred = register()
    assert lib.host_ui_state() == 3

    # Reboot -> state 0 (READY)
    lib.host_reboot()
    assert lib.host_ui_state() == 0

    # Silent preflight (up=False) must NOT fake a touch-confirmed success animation
    status, _, _ = assertion(cred.credential_id, extras={5: {'up': False}})
    assert status == 0
    assert lib.host_ui_state() == 0

    # Silent probe for unknown credential in idle state stays in state 0
    status, _, _ = assertion(b'0' * 16, extras={5: {'up': False}})
    assert status == 0x2e
    assert lib.host_ui_state() == 0

    # User-present assertion with touch -> state 3 (PASSED)
    status, _, _ = assertion(cred.credential_id)
    assert status == 0
    assert lib.host_ui_state() == 3

    # Touch timeout / denial -> state 4 (DENIED)
    lib.host_presence(0)
    status, _, _ = assertion(cred.credential_id)
    assert status == 0x2f
    assert lib.host_ui_state() == 4

    # Wrong PIN -> state 4 (DENIED)
    lib.host_presence(1)
    cp = client()
    cp.set_pin('123456')
    lib.host_reboot()
    with pytest.raises(PinError):
        cp.get_pin_token('wrong!')
    assert lib.host_ui_state() == 4
