# Cinder MetalFX MetalRHI bridge

UE 5.8 does not export its current Metal command buffer and its deprecated generic
native-command-buffer API returns null on Metal. The two patch files in this
directory add one C-linkage export to MetalRHI. It invokes a callback on the
active bottom-of-pipe graphics command buffer after closing any UE compute or
blit encoder. The callback must finish its own encoder and must not commit the
command buffer.

The plugin resolves the export dynamically on Mac so a binary can load against
an unpatched stock engine and fall back to UE's spatial upscale. An iOS build
made with the patched engine uses a strong reference so the bridge object is
retained by the monolithic static link. Unpatched iOS builds compile a fallback
path selected by `CINDER_METALFX_ENGINE_BRIDGE=0`.
