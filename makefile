!IF [if /I "%Platform%" EQU "x64" exit 1]
PLATFORM = windows
!ELSE
PLATFORM = windows32
!ENDIF

BIN_NAME = \
	cddNES.exe

OBJS = \
	src/cart.obj \
	src/apu.obj \
	src/cpu.obj \
	src/nes.obj \
	src/ppu.obj \
	ui/main.obj \
	ui/api.obj \
	ui/fs.obj \
	ui/args.obj \
	ui/settings.obj \
	ui/audio.obj \
	ui/render/render.obj \
	ui/render/gl.obj \
	ui/render/glproc.obj \
	ui/render/d3d9.obj \
	ui/render/d3d11.obj \
	ui/render/d3d12.obj \
	ui/render/ui.obj \
	ui/render/ui-d3d12-shim.obj

RESOURCES = \
	ui\assets\icon.res

RFLAGS = \
	/nologo

CFLAGS = \
	-Iui/include \
	-DWIN32_LEAN_AND_MEAN \
	-D_CRT_SECURE_NO_WARNINGS \
	/nologo \
	/wd4204 \
	/wd4121 \
	/GS- \
	/W4 \
	/O2 \
	/MT \
	/MP

!IFDEF DEBUG
CFLAGS = $(CFLAGS) /Oy- /Ob0 /Zi
!ELSE
CFLAGS = $(CFLAGS) /GL
!ENDIF

CPPFLAGS = $(CFLAGS)

LIBS = \
	ui/lib/$(PLATFORM)/SDL2.lib \
	ui/lib/$(PLATFORM)/imgui.lib \
	ui/lib/$(PLATFORM)/libuncurl.lib \
	ui/lib/$(PLATFORM)/cJSON.lib \
	ui/lib/$(PLATFORM)/libssl.lib \
	ui/lib/$(PLATFORM)/libcrypto.lib \
	libvcruntime.lib \
	libucrt.lib \
	libcmt.lib \
	kernel32.lib \
	gdi32.lib \
	winmm.lib \
	imm32.lib \
	shell32.lib \
	advapi32.lib \
	ole32.lib \
	oleaut32.lib \
	opengl32.lib \
	d3d9.lib \
	d3d11.lib \
	d3d12.lib \
	dxgi.lib \
	user32.lib \
	uuid.lib \
	version.lib \
	d3dcompiler.lib \
	ws2_32.lib

LD_FLAGS = \
	/subsystem:windows \
	/nodefaultlib \
	/nologo

!IFDEF DEBUG
LD_FLAGS = $(LD_FLAGS) /debug
!ELSE
LD_FLAGS = $(LD_FLAGS) /LTCG
!ENDIF

all: clean clear $(OBJS) $(RESOURCES)
	link *.obj $(LIBS) $(RESOURCES) /out:$(BIN_NAME) $(LD_FLAGS)

clean:
	-rd /s /q .vs
	del $(RESOURCES)
	del *.obj
	del *.exe
	del *.ilk
	del *.pdb

clear:
	cls
