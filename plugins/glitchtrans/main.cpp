// main.cpp - the glitchtrans plugin: the "glitch" transition, and a still glitch for scripts
//
//   [trans method=glitch time=600]                       the old picture breaks up into the new
//   [trans method=glitch time=300 strength=60 seed=7]    the same glitch every time
//   GlitchTrans.apply(layer, %[seed: 3, strength: 80])   one glitched frame of a layer's own
//                                                        picture, drawn over it
//
// Options (the transition's attributes, apply()'s dictionary), all but time optional (README.md):
//   time      ms (the transition's; apply() draws the height of a glitch)
//   style     mixed (default), digital, analog or mosh: which faults lead
//   seed      a number: the same seed draws the same glitch (default: a new one each time)
//   strength  0-100, how strong at its height (100)
//   bands, shift, split, noise, blocks, damage, jpeg, mosh, sort, analog, flash
//             each fault's amount (by default the style's; glitch.h)
//   step      about how many ms a jump of the glitch holds (50); apply() takes `frame`, which jump
//   spread    0-100, how far apart (of the whole time) the parts of the new picture arrive (60)
//
// SPDX-License-Identifier: 0BSD

#include "ncbind.hpp"

#include "glitch.h"

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace {

uint32_t freshSeed() {
	static uint32_t counter = 0;
	uint64_t t = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
	return uint32_t(t ^ (t >> 32)) ^ (++counter * 0x9e3779b9u);
}

bool optionInt(iTVPSimpleOptionProvider *options, const tjs_char *name, int &out) {
	tTJSVariant v;
	if (!options || TJS_FAILED(options->GetValue(name, &v)) || v.Type() == tvtVoid) return false;
	if (v.Type() == tvtString) {
		ttstr s = v;
		if (s.IsEmpty()) return false;
	}
	out = int(tjs_int(v));
	return true;
}

bool optionString(iTVPSimpleOptionProvider *options, const tjs_char *name, std::string &out) {
	tTJSVariant v;
	if (!options || TJS_FAILED(options->GetValue(name, &v)) || v.Type() != tvtString) return false;
	ttstr s = v;
	out.clear();
	for (const tjs_char *c = s.c_str(); *c; c++) out += *c < 128 ? char(*c) : '?';
	return true;
}

bool dictString(iTJSDispatch2 *dict, const tjs_char *name, std::string &out) {
	tTJSVariant v;
	if (!dict || TJS_FAILED(dict->PropGet(0, name, nullptr, &v, dict)) || v.Type() != tvtString) return false;
	ttstr s = v;
	out.clear();
	for (const tjs_char *c = s.c_str(); *c; c++) out += *c < 128 ? char(*c) : '?';
	return true;
}

bool dictInt(iTJSDispatch2 *dict, const tjs_char *name, int &out) {
	tTJSVariant v;
	if (!dict || TJS_FAILED(dict->PropGet(0, name, nullptr, &v, dict)) || v.Type() == tvtVoid) return false;
	out = int(tjs_int(v));
	return true;
}

template <typename Get, typename GetString>
glitch::Options readOptions(Get get, GetString getString) {
	glitch::Options o;
	int v;
	std::string style;
	if (getString(TJS_W("style"), style)) o.style = glitch::styleNamed(style);
	o.seed = get(TJS_W("seed"), v) ? uint32_t(v) : freshSeed();
	if (get(TJS_W("strength"), v)) o.strength = v;
	if (get(TJS_W("bands"), v)) o.bands = v;
	if (get(TJS_W("shift"), v)) o.shift = v;
	if (get(TJS_W("split"), v)) o.split = v;
	if (get(TJS_W("noise"), v)) o.noise = v;
	if (get(TJS_W("blocks"), v)) o.blocks = v;
	if (get(TJS_W("damage"), v)) o.damage = v;
	if (get(TJS_W("jpeg"), v)) o.jpeg = v;
	if (get(TJS_W("mosh"), v)) o.mosh = v;
	if (get(TJS_W("sort"), v)) o.sort = v;
	if (get(TJS_W("analog"), v)) o.analog = v;
	if (get(TJS_W("flash"), v)) o.flash = v;
	if (get(TJS_W("step"), v)) o.step = v;
	if (get(TJS_W("spread"), v)) o.spread = v;
	return o;
}

bool picture(iTVPScanLineProvider *p, glitch::Picture &pic) {
	if (!p) return false;
	tjs_int w = 0, h = 0, pitch = 0, bpp = 0;
	const void *line = nullptr;
	if (TJS_FAILED(p->GetWidth(&w)) || TJS_FAILED(p->GetHeight(&h)) || TJS_FAILED(p->GetPitchBytes(&pitch)) ||
	    TJS_FAILED(p->GetPixelFormat(&bpp)) || TJS_FAILED(p->GetScanLine(0, &line)))
		return false;
	if (bpp != 32) return false;
	pic.pixels = static_cast<const uint8_t *>(line);
	pic.pitch = long(pitch);
	pic.width = int(w);
	pic.height = int(h);
	return true;
}

// ------------------------------------------------------------------------
// Drawing on the engine's draw threads (-drawthread): bands of rows at once
// ------------------------------------------------------------------------

struct DrawTask {
	const glitch::Frame *frame;
	const glitch::Picture *from, *to;
	int left, top, w, h;
	uint8_t *dest;
	long destPitch;
	int destLeft, destTop;
};

void TJS_USERENTRY drawTaskEntry(void *p) {
	const DrawTask *t = static_cast<const DrawTask *>(p);
	glitch::draw(*t->frame, *t->from, *t->to, t->left, t->top, t->w, t->h, t->dest, t->destPitch, t->destLeft,
	             t->destTop);
}

void drawInParallel(const glitch::Frame &f, const glitch::Picture &from, const glitch::Picture &to, int left, int top,
                    int w, int h, uint8_t *dest, long destPitch, int destLeft, int destTop) {
	const int kMaxTasks = 16;
	int tasks = 1;
	if (long(w) * h >= 64 * 1024) {
		tasks = int(TVPGetThreadNum());
		tasks = tasks < 1 ? 1 : tasks > kMaxTasks ? kMaxTasks : tasks;
		if (tasks > h / 8) tasks = h / 8 > 0 ? h / 8 : 1;
	}
	if (tasks <= 1) {
		glitch::draw(f, from, to, left, top, w, h, dest, destPitch, destLeft, destTop);
		return;
	}
	DrawTask params[kMaxTasks];
	TVPBeginThreadTask(tasks);
	for (int i = 0; i < tasks; i++) {
		int y0 = h * i / tasks, y1 = h * (i + 1) / tasks;
		params[i] = DrawTask{&f, &from, &to, left, top + y0, w, y1 - y0, dest, destPitch, destLeft, destTop + y0};
		TVPExecThreadTask(&drawTaskEntry, TVP_THREAD_PARAM(&params[i]));
	}
	TVPEndThreadTask();
}

// ------------------------------------------------------------------------
// The transition
// ------------------------------------------------------------------------

class GlitchHandler : public iTVPDivisibleTransHandler {
	tjs_int refCount = 1;
	glitch::Options options;
	uint64_t time;
	int width, height;
	bool first = true;
	uint64_t startTick = 0;
	glitch::Frame frame;

public:
	GlitchHandler(const glitch::Options &o, uint64_t t, int w, int h) : options(o), time(t), width(w), height(h) {}
	virtual ~GlitchHandler() {}

	tjs_error TJS_INTF_METHOD AddRef() override {
		refCount++;
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD Release() override {
		if (refCount == 1) delete this;
		else refCount--;
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *) override { return TJS_S_OK; }

	tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick) override {
		if (first) {
			first = false;
			startTick = tick;
		}
		uint64_t elapsed = tick - startTick;
		if (elapsed > time) elapsed = time;
		frame = glitch::makeFrame(options, width, height, elapsed, time);
		return TJS_S_TRUE;
	}

	tjs_error TJS_INTF_METHOD EndProcess() override { return frame.t >= 1.0 ? TJS_S_FALSE : TJS_S_TRUE; }

	tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data) override {
		glitch::Picture from, to;
		if (!picture(data->Src1, from) || !picture(data->Src2, to)) return TJS_E_FAIL;
		void *destLine = nullptr;
		tjs_int destPitch = 0;
		if (TJS_FAILED(data->Dest->GetScanLineForWrite(0, &destLine)) || TJS_FAILED(data->Dest->GetPitchBytes(&destPitch)))
			return TJS_E_FAIL;
		if (from.width != width || from.height != height || to.width != width || to.height != height)
			return TJS_E_FAIL;
		drawInParallel(frame, from, to, data->Left, data->Top, data->Width, data->Height,
		               static_cast<uint8_t *>(destLine), long(destPitch), data->DestLeft, data->DestTop);
		return TJS_S_OK;
	}

	tjs_error TJS_INTF_METHOD MakeFinalImage(iTVPScanLineProvider **dest, iTVPScanLineProvider *,
	                                         iTVPScanLineProvider *src2) override {
		*dest = src2;
		return TJS_S_OK;
	}
};

class GlitchProvider : public iTVPTransHandlerProvider {
	tjs_int refCount = 1;

public:
	tjs_error TJS_INTF_METHOD AddRef() override {
		refCount++;
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD Release() override {
		if (refCount == 1) delete this;
		else refCount--;
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD GetName(const tjs_char **name) override {
		if (name) *name = TJS_W("glitch");
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD StartTransition(iTVPSimpleOptionProvider *options, iTVPSimpleImageProvider *,
	                                          tTVPLayerType, tjs_uint src1w, tjs_uint src1h, tjs_uint src2w,
	                                          tjs_uint src2h, tTVPTransType *type, tTVPTransUpdateType *updatetype,
	                                          iTVPBaseTransHandler **handler) override {
		if (type) *type = ttExchange;
		if (updatetype) *updatetype = tutDivisible;
		if (!handler || !options) return TJS_E_FAIL;
		if (src1w != src2w || src1h != src2h) return TJS_E_FAIL; // the two pictures must be the same size
		int t = 0;
		if (!optionInt(options, TJS_W("time"), t)) return TJS_E_FAIL; // a transition needs its time
		if (t < 2) t = 2;
		glitch::Options o = readOptions([&](const tjs_char *n, int &v) { return optionInt(options, n, v); },
		                                [&](const tjs_char *n, std::string &v) { return optionString(options, n, v); });
		*handler = new GlitchHandler(o, uint64_t(t), int(src1w), int(src1h));
		return TJS_S_OK;
	}
};

GlitchProvider *provider = nullptr;

bool getInt(iTJSDispatch2 *obj, const tjs_char *name, tjs_int64 &out) {
	tTJSVariant v;
	if (TJS_FAILED(obj->PropGet(0, name, nullptr, &v, obj))) return false;
	if (v.Type() != tvtInteger && v.Type() != tvtReal) return false;
	out = v.AsInteger();
	return true;
}

} // namespace

// ------------------------------------------------------------------------
// GlitchTrans: for scripts
// ------------------------------------------------------------------------

class GlitchTrans {
public:
	static ttstr getVersion() { return TJS_W("2.0"); }

	// One glitched frame of a layer's own picture, drawn over it; false when it has none
	static bool apply(tTJSVariant layer, tTJSVariant opts) {
		if (layer.Type() != tvtObject || !layer.AsObjectNoAddRef()) return false;
		iTJSDispatch2 *obj = layer.AsObjectNoAddRef();
		iTJSDispatch2 *dict = opts.Type() == tvtObject ? opts.AsObjectNoAddRef() : nullptr;
		tjs_int64 w = 0, h = 0, pitch = 0, buf = 0;
		if (!getInt(obj, TJS_W("imageWidth"), w) || !getInt(obj, TJS_W("imageHeight"), h) || w <= 0 || h <= 0)
			return false;
		// the writable buffer (the engine copies a shared picture first), then its pitch
		if (!getInt(obj, TJS_W("mainImageBufferForWrite"), buf) || buf == 0 ||
		    !getInt(obj, TJS_W("mainImageBufferPitch"), pitch))
			return false;
		glitch::Options o = readOptions([&](const tjs_char *n, int &v) { return dictInt(dict, n, v); },
		                                [&](const tjs_char *n, std::string &v) { return dictString(dict, n, v); });
		int frameIndex = 0;
		dictInt(dict, TJS_W("frame"), frameIndex);
		uint8_t *px = reinterpret_cast<uint8_t *>(static_cast<intptr_t>(buf));
		// the picture as it was, as the source
		std::vector<uint8_t> copy(size_t(w) * size_t(h) * 4);
		for (tjs_int64 y = 0; y < h; y++) std::memcpy(&copy[size_t(y * w * 4)], px + pitch * y, size_t(w) * 4);
		glitch::Picture src;
		src.pixels = copy.data();
		src.pitch = long(w * 4);
		src.width = int(w);
		src.height = int(h);
		int step = o.step > 0 ? o.step : 50;
		glitch::Frame f = glitch::makeFrame(o, int(w), int(h), uint64_t(frameIndex < 0 ? 0 : frameIndex) * uint64_t(step), 0);
		drawInParallel(f, src, src, 0, 0, int(w), int(h), px, long(pitch), 0, 0);
		// tell the engine the picture changed
		tTJSVariant result;
		obj->FuncCall(0, TJS_W("update"), nullptr, &result, 0, nullptr, obj);
		return true;
	}
};

NCB_REGISTER_CLASS(GlitchTrans) {
	NCB_PROPERTY_RO(version, getVersion);
	NCB_METHOD(apply);
}

static void glitchtransRegist() {
	if (!provider) {
		provider = new GlitchProvider();
		TVPAddTransHandlerProvider(provider);
	}
}

static void glitchtransUnregist() {
	if (provider) {
		TVPRemoveTransHandlerProvider(provider);
		provider->Release();
		provider = nullptr;
	}
}

NCB_POST_REGIST_CALLBACK(glitchtransRegist);
NCB_PRE_UNREGIST_CALLBACK(glitchtransUnregist);
