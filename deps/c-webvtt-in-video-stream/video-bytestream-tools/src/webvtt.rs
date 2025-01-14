use crate::h264::{write_sei_header, CountingSink};
use byteorder::{BigEndian, WriteBytesExt};
use std::{io::Write, time::Duration};
use uuid::{uuid, Uuid};

pub const USER_DATA_UNREGISTERED: usize = 5;
pub const HEADER_GUID: Uuid = uuid!("cc7124bd-5f1c-4592-b27a-e2d9d218ef9e");
pub const PAYLOAD_GUID: Uuid = uuid!("a0cb4dd1-9db2-4635-a76b-1c9fefd6c37b");

trait WriteCStrExt: Write {
    fn write_c_str(&mut self, string: &str) -> std::io::Result<()> {
        self.write_all(string.as_bytes())?;
        self.write_u8(0)?;
        Ok(())
    }
}

impl<W: Write + ?Sized> WriteCStrExt for W {}

pub struct WebvttTrack<'a> {
    pub default: bool,
    pub autoselect: bool,
    pub forced: bool,
    pub name: &'a str,
    pub language: &'a str,
    pub assoc_language: Option<&'a str>,
    pub characteristics: Option<&'a str>,
}

pub(crate) fn write_webvtt_header<W: Write + ?Sized>(
    writer: &mut W,
    max_latency_to_video: Duration,
    send_frequency_hz: u8,
    subtitle_tracks: &[WebvttTrack],
) -> std::io::Result<()> {
    fn inner<W: ?Sized + Write>(
        writer: &mut W,
        max_latency_to_video: Duration,
        send_frequency_hz: u8,
        subtitle_tracks: &[WebvttTrack],
    ) -> std::io::Result<()> {
        writer.write_all(HEADER_GUID.as_bytes())?;
        writer.write_u16::<BigEndian>(max_latency_to_video.as_millis().try_into().unwrap())?;
        writer.write_u8(send_frequency_hz)?;
        writer.write_u8(subtitle_tracks.len().try_into().unwrap())?;
        for track in subtitle_tracks {
            let flags = {
                let mut flags: u8 = 0;
                if track.default {
                    flags |= 0b1000_0000;
                }
                if track.autoselect {
                    flags |= 0b0100_0000;
                }
                if track.forced {
                    flags |= 0b0010_0000;
                }
                if track.assoc_language.is_some() {
                    flags |= 0b0001_0000;
                }
                if track.characteristics.is_some() {
                    flags |= 0b0000_1000;
                }
                flags
            };
            writer.write_u8(flags)?;
            writer.write_c_str(track.name)?;
            writer.write_c_str(track.language)?;
            if let Some(assoc_language) = track.assoc_language {
                writer.write_c_str(assoc_language)?;
            }
            if let Some(characteristics) = track.characteristics {
                writer.write_c_str(characteristics)?;
            }
        }
        Ok(())
    }
    let mut count = CountingSink::new();
    inner(
        &mut count,
        max_latency_to_video,
        send_frequency_hz,
        subtitle_tracks,
    )?;
    write_sei_header(writer, USER_DATA_UNREGISTERED, count.count())?;
    inner(
        writer,
        max_latency_to_video,
        send_frequency_hz,
        subtitle_tracks,
    )
}

pub(crate) fn write_webvtt_payload<W: Write + ?Sized>(
    writer: &mut W,
    track_index: u8,
    chunk_number: u64,
    chunk_version: u8,
    video_offset: Duration,
    webvtt_payload: &str, // TODO: replace with string type that checks for interior NULs
) -> std::io::Result<()> {
    fn inner<W: ?Sized + Write>(
        writer: &mut W,
        track_index: u8,
        chunk_number: u64,
        chunk_version: u8,
        video_offset: Duration,
        webvtt_payload: &str,
    ) -> std::io::Result<()> {
        writer.write_all(PAYLOAD_GUID.as_bytes())?;
        writer.write_u8(track_index)?;
        writer.write_u64::<BigEndian>(chunk_number)?;
        writer.write_u8(chunk_version)?;
        writer.write_u16::<BigEndian>(video_offset.as_millis().try_into().unwrap())?;
        writer.write_c_str(webvtt_payload)?;
        Ok(())
    }

    let mut count = CountingSink::new();
    inner(
        &mut count,
        track_index,
        chunk_number,
        chunk_version,
        video_offset,
        webvtt_payload,
    )?;
    write_sei_header(writer, USER_DATA_UNREGISTERED, count.count())?;
    inner(
        writer,
        track_index,
        chunk_number,
        chunk_version,
        video_offset,
        webvtt_payload,
    )
}

pub trait WebvttWrite {
    fn write_webvtt_header(
        &mut self,
        max_latency_to_video: Duration,
        send_frequency_hz: u8,
        subtitle_tracks: &[WebvttTrack],
    ) -> std::io::Result<()>;

    fn write_webvtt_payload(
        &mut self,
        track_index: u8,
        chunk_number: u64,
        chunk_version: u8,
        video_offset: Duration,
        webvtt_payload: &str, // TODO: replace with string type that checks for interior NULs
    ) -> std::io::Result<()>;
}
