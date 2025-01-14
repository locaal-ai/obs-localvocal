use std::{
    error::Error,
    ffi::{c_char, CStr},
    time::Duration,
};
use strum_macros::FromRepr;
use video_bytestream_tools::{
    h264::{self, H264ByteStreamWrite, NalHeader, NalUnitWrite, RbspWrite},
    webvtt::WebvttWrite,
};
use webvtt_in_video_stream::{WebvttMuxer, WebvttMuxerBuilder, WebvttString};

#[no_mangle]
pub extern "C" fn webvtt_create_muxer_builder(
    latency_to_video_in_msecs: u16,
    send_frequency_hz: u8,
    video_frame_time_in_nsecs: u64,
) -> Box<WebvttMuxerBuilder> {
    Box::new(WebvttMuxerBuilder::new(
        Duration::from_millis(latency_to_video_in_msecs.into()),
        send_frequency_hz,
        Duration::from_nanos(video_frame_time_in_nsecs),
    ))
}

fn turn_into_webvtt_string(ptr: *const c_char) -> Option<WebvttString> {
    if ptr.is_null() {
        return None;
    }
    let c_str = unsafe { CStr::from_ptr(ptr) };
    WebvttString::from_string(c_str.to_string_lossy().into_owned()).ok()
}

#[no_mangle]
pub extern "C" fn webvtt_muxer_builder_add_track(
    builder: Option<&mut WebvttMuxerBuilder>,
    default: bool,
    autoselect: bool,
    forced: bool,
    name_ptr: *const c_char,
    language_ptr: *const c_char,
    assoc_language_ptr: *const c_char,
    characteristics_ptr: *const c_char,
) -> bool {
    let Some(builder) = builder else { return false };
    let Some(name) = turn_into_webvtt_string(name_ptr) else {
        return false;
    };
    let Some(language) = turn_into_webvtt_string(language_ptr) else {
        return false;
    };
    let assoc_language = turn_into_webvtt_string(assoc_language_ptr);
    let characteristics = turn_into_webvtt_string(characteristics_ptr);
    builder
        .add_track(
            default,
            autoselect,
            forced,
            name,
            language,
            assoc_language,
            characteristics,
        )
        .is_ok()
}

#[no_mangle]
pub extern "C" fn webvtt_muxer_builder_create_muxer(
    muxer_builder: Option<Box<WebvttMuxerBuilder>>,
) -> Option<Box<WebvttMuxer>> {
    muxer_builder.map(|builder| Box::new(builder.create_muxer()))
}

#[no_mangle]
pub extern "C" fn webvtt_muxer_free(_: Option<Box<WebvttMuxer>>) {}

#[no_mangle]
pub extern "C" fn webvtt_muxer_add_cue(
    muxer: Option<&WebvttMuxer>,
    track: u8,
    start_time_in_msecs: u64,
    duration_in_msecs: u64,
    text_ptr: *const c_char,
) -> bool {
    let Some(muxer) = muxer else { return false };
    let Some(text) = turn_into_webvtt_string(text_ptr) else {
        return false;
    };
    muxer
        .add_cue(
            track,
            Duration::from_millis(start_time_in_msecs),
            Duration::from_millis(duration_in_msecs),
            text,
        )
        .is_ok()
}

#[derive(FromRepr, Copy, Clone)]
#[repr(u8)]
enum CodecFlavor {
    H264Avcc1,
    H264Avcc2,
    H264Avcc4,
    H264AnnexB,
}

impl CodecFlavor {
    fn into_internal(self) -> CodecFlavorInternal {
        match self {
            CodecFlavor::H264Avcc1 => CodecFlavorInternal::H264(CodecFlavorH264::Avcc(1)),
            CodecFlavor::H264Avcc2 => CodecFlavorInternal::H264(CodecFlavorH264::Avcc(2)),
            CodecFlavor::H264Avcc4 => CodecFlavorInternal::H264(CodecFlavorH264::Avcc(4)),
            CodecFlavor::H264AnnexB => CodecFlavorInternal::H264(CodecFlavorH264::AnnexB),
        }
    }
}

enum CodecFlavorH264 {
    Avcc(usize),
    AnnexB,
}

enum CodecFlavorInternal {
    H264(CodecFlavorH264),
}

pub struct WebvttBuffer(Vec<u8>);

#[no_mangle]
pub extern "C" fn webvtt_muxer_try_mux_into_bytestream(
    muxer: Option<&WebvttMuxer>,
    video_timestamp_in_nsecs: u64,
    add_header: bool,
    codec_flavor: u8,
) -> Option<Box<WebvttBuffer>> {
    fn mux_into_bytestream<'a, W: WebvttWrite + 'a>(
        muxer: &WebvttMuxer,
        video_timestamp: Duration,
        add_header: bool,
        buffer: &'a mut Vec<u8>,
        init: impl Fn(&'a mut Vec<u8>) -> Result<W, Box<dyn Error>>,
        finish: impl Fn(W) -> Result<(), Box<dyn Error>>,
    ) -> Result<bool, Box<dyn Error>> {
        let mut writer = init(buffer)?;
        if !muxer.try_mux_into_bytestream(video_timestamp, add_header, &mut writer)? {
            return Ok(false);
        }
        finish(writer)?;
        Ok(true)
    }

    fn create_nal_header() -> NalHeader {
        NalHeader::from_nal_unit_type_and_nal_ref_idc(h264_reader::nal::UnitType::SEI, 0).unwrap()
    }

    fn inner(
        muxer: Option<&WebvttMuxer>,
        video_timestamp_in_nsecs: u64,
        add_header: bool,
        codec_flavor: u8,
    ) -> Option<Box<WebvttBuffer>> {
        let muxer = muxer?;
        let video_timestamp = Duration::from_nanos(video_timestamp_in_nsecs);
        let codec_flavor = CodecFlavor::from_repr(codec_flavor)?;
        let mut buffer = vec![];
        let data_written = match codec_flavor.into_internal() {
            CodecFlavorInternal::H264(CodecFlavorH264::AnnexB) => mux_into_bytestream(
                muxer,
                video_timestamp,
                add_header,
                &mut buffer,
                |buffer| {
                    Ok(h264::annex_b::AnnexBWriter::new(buffer)
                        .start_write_nal_unit()?
                        .write_nal_header(create_nal_header())?)
                },
                |write| {
                    write.finish_rbsp()?;
                    Ok(())
                },
            )
            .ok()?,
            CodecFlavorInternal::H264(CodecFlavorH264::Avcc(length_size)) => mux_into_bytestream(
                muxer,
                video_timestamp,
                add_header,
                &mut buffer,
                |buffer| {
                    Ok(h264::avcc::AVCCWriter::new(length_size, buffer)?
                        .start_write_nal_unit()?
                        .write_nal_header(create_nal_header())?)
                },
                |write| {
                    write.finish_rbsp()?;
                    Ok(())
                },
            )
            .ok()?,
        };
        if !data_written {
            return None;
        }
        Some(Box::new(WebvttBuffer(buffer)))
    }
    inner(muxer, video_timestamp_in_nsecs, add_header, codec_flavor)
}

#[no_mangle]
pub extern "C" fn webvtt_buffer_data(buffer: Option<&WebvttBuffer>) -> *const u8 {
    buffer.map(|b| b.0.as_ptr()).unwrap_or(std::ptr::null())
}

#[no_mangle]
pub extern "C" fn webvtt_buffer_length(buffer: Option<&WebvttBuffer>) -> usize {
    buffer.map(|b| b.0.len()).unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn webvtt_buffer_free(_: Option<Box<WebvttBuffer>>) {}
