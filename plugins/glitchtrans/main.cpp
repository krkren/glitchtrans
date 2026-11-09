// main.cpp - the glitchtrans plugin: the "glitch" transition, and a still glitch for scripts
//
//   [trans method=glitch time=600]                       the old picture breaks up into the new
//   [trans method=glitch time=300 strength=60 seed=7]    the same glitch every time
//   GlitchTrans.apply(layer, %[seed: 3, strength: 80])   one glitched frame of a layer's own
//                                                        picture, drawn over it
//
// Options (the transition's attributes, apply()'s dictionary), all but time optional:
//   time      ms (the transition's; apply() draws the height of a glitch)
//   seed      a number: the same seed draws the same glitch (default: a new one each time)
//   strength  0-100, how strong at its height (100)
//   bands     how many bands the picture is cut into (18)
//   shift     the most a band moves sideways, px (a twelfth of the width)
//   split     the most the colours move apart, px (width / 96)
//   noise     0-100, how many rows of static (30)
//   blocks    displaced rectangles at the height (12)
//   step      ms between the glitch's jumps (50); apply() takes `frame`, which jump to draw
//   spread    0-100, how far apart (of the whole time) the stripes of the new picture arrive (60)
//
// SPDX-License-Identifier: 0BSD

#include "ncbind.hpp"

#include "glitch.h"

#include <chrono>
#include <cstring>
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

bool dictInt(iTJSDispatch2 *dict, const tjs_char *name, int &out) {
	tTJSVariant v;
	if (!dict || TJS_FAILED(dict->PropGet(0, name, nullptr, &v, dict)) || v.Type() == tvtVoid) return false;
	out = int(tjs_int(v));
	return true;
}

template <typename Get>
glitch::Options readOptions(Get get) {
	glitch::Options o;
	int v;
	o.seed = get(TJS_W("seed"), v) ? uint32_t(v) : freshSeed();
	if (get(TJS_W("strength"), v)) o.strength = v;
	if (get(TJS_W("bands"), v)) o.bands = v;
	if (get(TJS_W("shift"), v)) o.shift = v;
	if (get(TJS_W("split"), v)) o.split = v;
	if (get(TJS_W("noise"), v)) o.noise = v;
	if (get(TJS_W("blocks"), v)) o.blocks = v;
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
		glitch::draw(frame, from, to, data->Left, data->Top, data->Width, data->Height,
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
		glitch::Options o = readOptions([&](const tjs_char *n, int &v) { return optionInt(options, n, v); });
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
	static ttstr getVersion() { return TJS_W("1.0"); }

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
		glitch::Options o = readOptions([&](const tjs_char *n, int &v) { return dictInt(dict, n, v); });
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
		glitch::draw(f, src, src, 0, 0, int(w), int(h), px, long(pitch), 0, 0);
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
