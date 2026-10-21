// Private Vulkan Xlib WSI admission; no connection identity survives this call.
use std::ffi::{c_char, c_int, c_void};

#[repr(C)]
#[derive(Clone, Copy)]
struct Cookie { sequence: u32 }
#[repr(C)]
struct ExtensionReply {
    response: u8, pad: u8, sequence: u16, length: u32,
    present: u8, opcode: u8, first_event: u8, first_error: u8,
}
#[repr(C)]
struct VersionReply {
    response: u8, pad: u8, sequence: u16, length: u32, major: u32, minor: u32,
}
#[repr(C)]
struct VersionRequest { opcode: u8, minor: u8, length: u16, major: u32, version_minor: u32 }
#[repr(C)]
struct Request { count: usize, extension: *mut c_void, opcode: u8, is_void: u8 }
#[repr(C)]
struct IoVec { base: *mut c_void, len: usize }
extern "C" { fn free(pointer: *mut c_void); }

pub unsafe fn prepare(display: *mut c_void) -> Result<(), String> {
    if display.is_null() { return Err("Vulkan X11 WSI requires a live Display".into()); }
    // These libraries register no display close hooks; temporary loading retains
    // neither the caller's Display nor its connection. No new link dependency.
    let xlib = unsafe { libloading::Library::new("libX11.so.6") }
        .map_err(|e| format!("Vulkan X11 WSI cannot load Xlib: {e}"))?;
    let xlibxcb = unsafe { libloading::Library::new("libX11-xcb.so.1") }
        .map_err(|e| format!("Vulkan X11 WSI cannot load Xlib/XCB support: {e}"))?;
    let xcb = unsafe { libloading::Library::new("libxcb.so.1") }
        .map_err(|e| format!("Vulkan X11 WSI cannot load XCB: {e}"))?;
    unsafe {
        let flush = xlib.get::<unsafe extern "C" fn(*mut c_void) -> c_int>(b"XFlush\0")
            .map_err(|e| format!("Vulkan X11 WSI missing XFlush: {e}"))?;
        let connection = xlibxcb.get::<unsafe extern "C" fn(*mut c_void) -> *mut c_void>(b"XGetXCBConnection\0")
            .map_err(|e| format!("Vulkan X11 WSI missing XGetXCBConnection: {e}"))?;
        let query = xcb.get::<unsafe extern "C" fn(*mut c_void, u16, *const c_char) -> Cookie>(b"xcb_query_extension\0")
            .map_err(|e| format!("Vulkan X11 WSI missing extension query: {e}"))?;
        let extension_reply = xcb.get::<unsafe extern "C" fn(*mut c_void, Cookie, *mut *mut c_void) -> *mut ExtensionReply>(b"xcb_query_extension_reply\0")
            .map_err(|e| format!("Vulkan X11 WSI missing extension reply: {e}"))?;
        let send = xcb.get::<unsafe extern "C" fn(*mut c_void, c_int, *mut IoVec, *const Request) -> u32>(b"xcb_send_request\0")
            .map_err(|e| format!("Vulkan X11 WSI missing request support: {e}"))?;
        let wait = xcb.get::<unsafe extern "C" fn(*mut c_void, u32, *mut *mut c_void) -> *mut VersionReply>(b"xcb_wait_for_reply\0")
            .map_err(|e| format!("Vulkan X11 WSI missing reply support: {e}"))?;
        flush(display);
        let c = connection(display);
        if c.is_null() { return Err("Vulkan X11 WSI has no XCB connection".into()); }
        let mut error = std::ptr::null_mut();
        let extension = extension_reply(c, query(c, 6, b"XFIXES".as_ptr().cast()), &mut error);
        if !error.is_null() || extension.is_null() {
            free(error); free(extension.cast());
            return Err("Vulkan X11 WSI failed to query XFIXES extension".into());
        }
        let present = (*extension).present != 0;
        let opcode = (*extension).opcode;
        let valid_extension = (*extension).response == 1 && (*extension).length == 0
            && (!present || opcode >= 128);
        free(extension.cast());
        // CoinRender's Vulkan Xlib presentation profile requires region-capable
        // connections before hardware WSI. This is not a universal Vulkan rule.
        if !valid_extension { return Err("Vulkan X11 WSI received an invalid XFIXES extension reply".into()); }
        if !present {
            return Err("CoinRender Vulkan Xlib presentation requires XFIXES version 2 (extension absent)".into());
        }

        // fixesproto section 4 requires negotiation on this actual connection.
        // Never trust a driver cache keyed by a recycled XCB address.
        let mut version = VersionRequest { opcode: 0, minor: 0, length: 0, major: 6, version_minor: 0 };
        let mut vectors = [
            IoVec { base: std::ptr::null_mut(), len: 0 },
            IoVec { base: std::ptr::null_mut(), len: 0 },
            IoVec { base: (&mut version as *mut VersionRequest).cast(), len: std::mem::size_of::<VersionRequest>() },
            IoVec { base: std::ptr::null_mut(), len: 0 },
        ];
        let request = Request { count: 2, extension: std::ptr::null_mut(), opcode, is_void: 0 };
        let sequence = send(c, 1 /* XCB_REQUEST_CHECKED */, vectors.as_mut_ptr().add(2), &request);
        let reply = if sequence == 0 { std::ptr::null_mut() } else { wait(c, sequence, &mut error) };
        if !error.is_null() || reply.is_null() {
            free(error); free(reply.cast());
            return Err("Vulkan X11 WSI XFIXES QueryVersion failed".into());
        }
        let valid_version = (*reply).response == 1 && (*reply).length == 0
            && ((*reply).major < 6 || ((*reply).major == 6 && (*reply).minor == 0));
        let regions = valid_version && (*reply).major >= 2;
        free(reply.cast());
        if !valid_version { return Err("Vulkan X11 WSI received an invalid XFIXES version reply".into()); }
        if !regions { return Err("Vulkan X11 WSI requires XFIXES version 2 for region requests".into()); }
    }
    Ok(())
}
