use super::{
    H264ByteStreamWrite, NalHeader, NalUnitWrite, NalUnitWriter, RbspWrite, RbspWriter, Result,
};
use crate::webvtt::{WebvttTrack, WebvttWrite};
use byteorder::WriteBytesExt;
use std::{io::Write, time::Duration};

pub struct AnnexBWriter<W: ?Sized + Write> {
    leading_zero_8bits_written: bool,
    inner: W,
}

impl<W: Write> AnnexBWriter<W> {
    pub fn new(inner: W) -> Self {
        Self {
            leading_zero_8bits_written: false,
            inner,
        }
    }
}

impl<W: Write> H264ByteStreamWrite<W> for AnnexBWriter<W> {
    type Writer = AnnexBNalUnitWriter<W>;

    fn start_write_nal_unit(mut self) -> Result<AnnexBNalUnitWriter<W>> {
        if !self.leading_zero_8bits_written {
            self.inner.write_u8(0)?;
            self.leading_zero_8bits_written = true;
        }
        self.inner.write_all(&[0, 0, 1])?;
        Ok(AnnexBNalUnitWriter {
            inner: NalUnitWriter::new(self.inner),
        })
    }
}

pub struct AnnexBNalUnitWriter<W: ?Sized + Write> {
    inner: NalUnitWriter<W>,
}

impl<W: Write> AnnexBNalUnitWriter<W> {
    fn _nal_unit_writer(&mut self) -> &mut NalUnitWriter<W> {
        &mut self.inner
    }
}

impl<W: Write> NalUnitWrite<W> for AnnexBNalUnitWriter<W> {
    type Writer = AnnexBRbspWriter<W>;

    fn write_nal_header(self, nal_header: NalHeader) -> Result<AnnexBRbspWriter<W>> {
        self.inner
            .write_nal_header(nal_header)
            .map(|inner| AnnexBRbspWriter { inner })
    }
}

pub struct AnnexBRbspWriter<W: ?Sized + Write> {
    inner: RbspWriter<W>,
}

impl<W: ?Sized + Write> AnnexBRbspWriter<W> {}

impl<W: Write> RbspWrite<W> for AnnexBRbspWriter<W> {
    type Writer = AnnexBWriter<W>;

    fn finish_rbsp(self) -> Result<Self::Writer> {
        self.inner
            .finish_rbsp()
            .map(|writer| AnnexBWriter::new(writer))
    }
}

impl<W: Write + ?Sized> WebvttWrite for AnnexBRbspWriter<W> {
    fn write_webvtt_header(
        &mut self,
        max_latency_to_video: Duration,
        send_frequency_hz: u8,
        subtitle_tracks: &[WebvttTrack],
    ) -> std::io::Result<()> {
        self.inner
            .write_webvtt_header(max_latency_to_video, send_frequency_hz, subtitle_tracks)
    }

    fn write_webvtt_payload(
        &mut self,
        track_index: u8,
        chunk_number: u64,
        chunk_version: u8,
        video_offset: Duration,
        webvtt_payload: &str, // TODO: replace with string type that checks for interior NULs
    ) -> std::io::Result<()> {
        self.inner.write_webvtt_payload(
            track_index,
            chunk_number,
            chunk_version,
            video_offset,
            webvtt_payload,
        )
    }
}
