#include "d3d9.h"

#include <stdio.h>
#include <math.h>

#include <windows.h>
#include <d3d9.h>

#include "shaders/d3d9/vs.h"
#include "shaders/d3d9/ps.h"

#define DUMMY_WIN_CLASS "d3d9_dummy_window"

struct d3d9 {
	D3DDISPLAYMODEEX mode;
	IDirect3D9Ex *factory;
	IDirect3DDevice9Ex *device;
	IDirect3DDevice9 *device_og;
	IDirect3DSwapChain9Ex *swap_chain;
	IDirect3DVertexShader9 *vs;
	IDirect3DPixelShader9 *ps;
	IDirect3DVertexBuffer9 *vb;
	IDirect3DVertexDeclaration9 *vd;
	IDirect3DIndexBuffer9 *ib;
	IDirect3DTexture9 *tex;
	IDirect3DSurface9 *render_target;

	HWND hwnd;
	bool vsync;
	bool scene_begun;
	bool headless;
	enum sampler sampler;
	uint32_t w;
	uint32_t h;
};

void d3d9_set_sampler(struct render_mod *mod, enum sampler sampler)
{
	struct d3d9 *d3d9 = (struct d3d9 *) mod;

	d3d9->sampler = sampler;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int32_t d3d9_init(struct render_mod **mod_out, void *window, bool vsync,
	uint32_t width, uint32_t height, enum sampler sampler)
{
	struct d3d9 **d3d9_out = (struct d3d9 **) mod_out;
	struct d3d9 *d3d9 = *d3d9_out = (struct d3d9 *) calloc(1, sizeof(struct d3d9));
	d3d9->hwnd = (HWND) window;
	d3d9->sampler = sampler;
	d3d9->vsync = vsync;

	int32_t r = 0;
	D3DPRESENT_PARAMETERS pp = {0};
	IDirect3DSwapChain9 *swap_chain = NULL;

	if (!d3d9->hwnd) {
		d3d9->headless = true;

		WNDCLASSEX wx = {0};
		wx.cbSize = sizeof(WNDCLASSEX);
		wx.lpfnWndProc = (WNDPROC) WndProc;
		wx.hInstance = GetModuleHandle(NULL);
		wx.lpszClassName = DUMMY_WIN_CLASS;

		ATOM a = RegisterClassEx(&wx);
		if (a == 0) {printf("RegisterClassEx=0\n"); r = -1; goto except;}

		d3d9->hwnd = CreateWindowEx(0, DUMMY_WIN_CLASS, NULL, WS_POPUP,
			0, 0, 1, 1, NULL, NULL, NULL, NULL);
		if (!d3d9->hwnd) {printf("CreateWindowEx=NULL\n"); r = -1; goto except;}
	}

	int32_t e = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9->factory);
	if (e != D3D_OK) {printf("Direct3DCreate9Ex=%d\n", e); r = -1; goto except;}

	d3d9->mode.Size = sizeof(D3DDISPLAYMODEEX);
	e = d3d9->factory->GetAdapterDisplayModeEx(D3DADAPTER_DEFAULT, &d3d9->mode, NULL);
	if (e != D3D_OK) {printf("GetAdapterDisplayModeEx=%d\n", e); r = -1; goto except;}

	pp.BackBufferFormat = d3d9->mode.Format;
	pp.BackBufferCount = 1;
	pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
	pp.hDeviceWindow = d3d9->hwnd;
	pp.Windowed = TRUE;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_ENABLE_PRESENTSTATS |
		D3DCREATE_PUREDEVICE | D3DCREATE_NOWINDOWCHANGES | D3DCREATE_MULTITHREADED |
		D3DCREATE_DISABLE_PSGP_THREADING;

	e = d3d9->factory->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3d9->hwnd,
		flags, &pp, NULL, &d3d9->device);
	if (e != D3D_OK) {printf("CreateDeviceEx=%d\n", e); r = -1; goto except;}

	e = d3d9->device->QueryInterface(__uuidof(IDirect3DDevice9), (void **) &d3d9->device_og);
	if (e != D3D_OK) {printf("QueryInterface=%d\n", e); r = -1; goto except;}

	if (!d3d9->headless) {
		e = d3d9->device->GetSwapChain(0, &swap_chain);
		if (e != D3D_OK) {printf("GetSwapChain=%d\n", e); r = -1; goto except;}

		e = swap_chain->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), (void **) &d3d9->swap_chain);
		if (e != D3D_OK) {printf("QueryInterface=%d\n", e); r = -1; goto except;}

		e = d3d9->device->SetMaximumFrameLatency(1);
		if (e != S_OK) {printf("SetMaximumFrameLatency=%d\n", e); r = -1; goto except;}
	}

	e = d3d9->device->CreateVertexShader((DWORD *) VS, &d3d9->vs);
	if (e != D3D_OK) {printf("CreateVertexShader=%d\n", e); r = -1; goto except;}

	e = d3d9->device->CreatePixelShader((DWORD *) PS, &d3d9->ps);
	if (e != D3D_OK) {printf("CreatePixelShader=%d\n", e); r = -1; goto except;}

	//vertex buffer
	float vertex_data[] = {
		-1.0f, -1.0f,  // position0 (bottom-left)
		 0.0f,  1.0f,  // texcoord0
		-1.0f,  1.0f,  // position1 (top-left)
		 0.0f,  0.0f,  // texcoord1
		 1.0f,  1.0f,  // position2 (top-right)
		 1.0f,  0.0f,  // texcoord2
		 1.0f, -1.0f,  // position3 (bottom-right)
		 1.0f,  1.0f,  // texcoord3
	};

	e = d3d9->device->CreateVertexBuffer(sizeof(vertex_data), 0, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
		D3DPOOL_DEFAULT, &d3d9->vb, NULL);
	if (e != D3D_OK) {printf("CreateVertexBuffer=%d\n", e); r = -1; goto except;}

	char *ptr = NULL;
	e = d3d9->vb->Lock(0, 0, (void **) &ptr, D3DLOCK_DISCARD);
	if (e != D3D_OK) {printf("Lock=%d\n", e); r = -1; goto except;}

	memcpy(ptr, vertex_data, sizeof(vertex_data));

	e = d3d9->vb->Unlock();
	if (e != D3D_OK) {printf("Unlock=%d\n", e); r = -1; goto except;}

	//vertex declaration (input layout)
	D3DVERTEXELEMENT9 dec[] = {
		{0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
		{0, 2 * sizeof(float), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
		D3DDECL_END()
	};

	e = d3d9->device->CreateVertexDeclaration(dec, &d3d9->vd);
	if (e != D3D_OK) {printf("CreateVertexDeclaration=%d\n", e); r = -1; goto except;}

	//index buffer
	DWORD index_data[] = {
		0, 1, 2,
		2, 3, 0
	};

	e = d3d9->device->CreateIndexBuffer(sizeof(index_data), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX32,
		D3DPOOL_DEFAULT, &d3d9->ib, NULL);
	if (e != D3D_OK) {printf("CreateIndexBuffer=%d\n", e); r = -1; goto except;}

	e = d3d9->ib->Lock(0, 0, (void **) &ptr, D3DLOCK_DISCARD);
	if (e != D3D_OK) {printf("Lock=%d\n", e); r = -1; goto except;}

	memcpy(ptr, index_data, sizeof(index_data));

	e = d3d9->ib->Unlock();
	if (e != D3D_OK) {printf("Unlock=%d\n", e); r = -1; goto except;}

	e = d3d9->device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC,
		D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d3d9->tex, NULL);
	if (e != D3D_OK) {printf("CreateTexture=%d\n", e); r = -1; goto except;}

	except:

	if (swap_chain)
		swap_chain->Release();

	if (r != 0)
		d3d9_destroy(mod_out);

	return r;
}

void d3d9_destroy(struct render_mod **mod_out)
{
	struct d3d9 **d3d9_out = (struct d3d9 **) mod_out;

	if (d3d9_out == NULL || *d3d9_out == NULL)
		return;

	struct d3d9 *d3d9 = *d3d9_out;

	if (d3d9->render_target)
		d3d9->render_target->Release();

	if (d3d9->tex)
		d3d9->tex->Release();

	if (d3d9->ib)
		d3d9->ib->Release();

	if (d3d9->vd)
		d3d9->vd->Release();

	if (d3d9->vb)
		d3d9->vb->Release();

	if (d3d9->ps)
		d3d9->ps->Release();

	if (d3d9->vs)
		d3d9->vs->Release();

	if (d3d9->swap_chain)
		d3d9->swap_chain->Release();

	if (d3d9->device_og)
		d3d9->device_og->Release();

	if (d3d9->device)
		d3d9->device->Release();

	if (d3d9->factory)
		d3d9->factory->Release();

	if (d3d9->headless) {
		if (d3d9->hwnd)
			DestroyWindow(d3d9->hwnd);

		UnregisterClass(DUMMY_WIN_CLASS, GetModuleHandle(NULL));
	}

	free(d3d9);
	*d3d9_out = NULL;
}

void d3d9_get_device(struct render_mod *mod, struct render_device **device, struct render_context **context)
{
	struct d3d9 *d3d9 = (struct d3d9 *) mod;

	*device = (struct render_device *) d3d9->device;
	*context = NULL;
}

static void d3d9_set_viewport(struct d3d9 *d3d9, uint32_t window_w, uint32_t window_h, float ratio)
{
	D3DVIEWPORT9 viewport = {0};
	viewport.Width = window_w;
	viewport.Height = lrint((float) viewport.Width / ratio);
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 0.0f;

	if ((float) window_h / 240.0f < ((float) window_w / ratio) / 256.0f) {
		viewport.Height = window_h;
		viewport.Width = lrint((float) viewport.Height * ratio);
	}

	viewport.X = (viewport.Width > window_w) ? 0 : (window_w - viewport.Width) / 2;
	viewport.Y = (viewport.Height > window_h) ? 0 : (window_h - viewport.Height) / 2;

	int32_t e = d3d9->device->SetViewport(&viewport);
	if (e != D3D_OK) printf("SetViewport=%d\n", e);
}

void d3d9_draw(struct render_mod *mod, uint32_t window_w, uint32_t window_h, uint32_t *pixels, uint32_t aspect)
{
	struct d3d9 *d3d9 = (struct d3d9 *) mod;

	D3DLOCKED_RECT rect = {0};

	D3DVIEWPORT9 viewport = {0};
	viewport.Width = window_w;
	viewport.Height = window_h;

	int32_t e = d3d9->device->SetViewport(&viewport);
	if (e != D3D_OK) {printf("SetViewport=%d\n", e); goto except;}

	e = d3d9->device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
	if (e != D3D_OK) {printf("Clear=%d\n", e); goto except;}

	e = d3d9->device->BeginScene();
	if (e != D3D_OK) {printf("BeginScene=%d\n", e); goto except;}
	d3d9->scene_begun = true;

	e = d3d9->tex->LockRect(0, &rect, NULL, D3DLOCK_DISCARD);
	if (e != D3D_OK) {printf("LockRect=%d\n", e); goto except;}

	for (int32_t y = 0; y < 240; y++)
		memcpy((uint8_t *) rect.pBits + (y * rect.Pitch), pixels + y * 256, sizeof(uint32_t) * 256);

	e = d3d9->tex->UnlockRect(0);
	if (e != D3D_OK) {printf("UnlockRect=%d\n", e); goto except;}

	if (!d3d9->render_target || window_w != d3d9->w || window_h != d3d9->h) {
		if (!d3d9->headless) {
			D3DPRESENT_PARAMETERS pp = {0};
			pp.BackBufferFormat = d3d9->mode.Format;
			pp.BackBufferCount = 1;
			pp.SwapEffect = D3DSWAPEFFECT_FLIPEX;
			pp.hDeviceWindow = d3d9->hwnd;
			pp.Windowed = TRUE;
			pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

			e = d3d9->device->ResetEx(&pp, NULL);
			if (e != D3D_OK) {printf("ResetEx=%d\n", e); goto except;}
		}

		if (d3d9->render_target) {
			d3d9->render_target->Release();
			d3d9->render_target = NULL;
		}

		d3d9->w = window_w;
		d3d9->h = window_h;

		e = d3d9->device->CreateRenderTarget(window_w, window_h, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
			0, false, &d3d9->render_target, NULL);
		if (e != D3D_OK) {printf("CreateRenderTarget=%d\n", e); goto except;}

	}

	d3d9->device->SetRenderTarget(0, d3d9->render_target);

	d3d9_set_viewport(d3d9, window_w, window_h, ASPECT_RATIO(aspect));

	//vertex/index state
	d3d9->device->SetVertexShader(d3d9->vs);
	d3d9->device->SetStreamSource(0, d3d9->vb, 0, 4 * sizeof(float));
	d3d9->device->SetVertexDeclaration(d3d9->vd);
	d3d9->device->SetIndices(d3d9->ib);

	float texel_offset[4] = {-1.0f / (float) window_w, 1.0f / (float) window_h, 0.0f, 0.0f};
	d3d9->device->SetVertexShaderConstantF(0, texel_offset, 1);

	//pixel shader
	d3d9->device->SetPixelShader(d3d9->ps);
	d3d9->device->SetTexture(0, d3d9->tex);

	//sampler state
	d3d9->device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	d3d9->device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	d3d9->device->SetSamplerState(0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);

	if (d3d9->sampler == SAMPLE_LINEAR) {
		d3d9->device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		d3d9->device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);

	} else {
		d3d9->device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		d3d9->device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	}

	e = d3d9->device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
	if (e != D3D_OK) {printf("DrawIndexedPrimitive=%d\n", e); return;}

	except:

	return;
}

enum ParsecStatus d3d9_submit_parsec(struct render_mod *mod, ParsecDSO *parsec)
{
	struct d3d9 *d3d9 = (struct d3d9 *) mod;

	return ParsecHostD3D9SubmitFrame(parsec, d3d9->device_og, d3d9->render_target);
}

void d3d9_present(struct render_mod *mod)
{
	struct d3d9 *d3d9 = (struct d3d9 *) mod;

	if (d3d9->scene_begun) {
		d3d9->scene_begun = false;

		int32_t e = d3d9->device->EndScene();

		if (e == D3D_OK) {
			if (!d3d9->headless) {
				IDirect3DSurface9 *back_buffer = NULL;
				e = d3d9->swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
				if (e != D3D_OK) {printf("GetBackBuffer=%d\n", e); return;}

				e = d3d9->device->StretchRect(d3d9->render_target, NULL, back_buffer, NULL, D3DTEXF_NONE);
				back_buffer->Release();
				if (e != D3D_OK) {printf("StretchRect=%d\n", e); return;}
			}

			e = d3d9->device->PresentEx(NULL, NULL, NULL, NULL, 0);
			if (e != D3D_OK && e != S_PRESENT_OCCLUDED && e != S_PRESENT_MODE_CHANGED)
				{printf("PresentEx=%d\n", e); return;}

		} else {
			printf("EndScene=%d\n", e);
		}
	}
}
