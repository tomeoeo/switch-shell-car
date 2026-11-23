#include "pad.h"

void pad_init(PadState *p) {
	padInitializeAny(p);
}

static float axis(s32 v) {
	float f = (float)v / 32767.0f;
	return f < -1 ? -1 : f > 1 ? 1 : f;
}

void pad_read(PadState *p, PadInput *out) {
	padUpdate(p);
	const u64 b = padGetButtons(p);
	static const struct {
		u64 hid;
		uint32_t pad;
	} map[] = {
	    {HidNpadButton_ZR, PAD_ZR},     {HidNpadButton_ZL, PAD_ZL},   {HidNpadButton_Left, PAD_LEFT},
	    {HidNpadButton_Right, PAD_RIGHT}, {HidNpadButton_Up, PAD_UP}, {HidNpadButton_Down, PAD_DOWN},
	    {HidNpadButton_A, PAD_A},       {HidNpadButton_B, PAD_B},     {HidNpadButton_X, PAD_X},
	    {HidNpadButton_Y, PAD_Y},       {HidNpadButton_L, PAD_L},     {HidNpadButton_R, PAD_R},
	    {HidNpadButton_Plus, PAD_PLUS}, {HidNpadButton_Minus, PAD_MINUS},
	};
	out->buttons = 0;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (b & map[i].hid)
			out->buttons |= map[i].pad;
	HidAnalogStickState l = padGetStickPos(p, 0), r = padGetStickPos(p, 1);
	out->lx = axis(l.x);
	out->ly = axis(l.y);
	out->rx = axis(r.x);
	out->ry = axis(r.y);
	out->connected = padIsConnected(p);
}
