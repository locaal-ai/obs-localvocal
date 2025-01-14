use crate::webvtt::{write_webvtt_header, write_webvtt_payload, WebvttTrack, WebvttWrite};
use byteorder::WriteBytesExt;
use h264_reader::nal::UnitType;
use std::{collections::VecDeque, io::Write, time::Duration};

type Result<T, E = std::io::Error> = std::result::Result<T, E>;

pub mod annex_b;
pub mod avcc;

pub trait H264ByteStreamWrite<W: ?Sized + Write> {
    type Writer: NalUnitWrite<W>;
    fn start_write_nal_unit(self) -> Result<Self::Writer>;
}

impl<W: Write> H264ByteStreamWrite<W> for W {
    type Writer = NalUnitWriter<W>;

    fn start_write_nal_unit(self) -> Result<Self::Writer> {
        Ok(NalUnitWriter::new(self))
    }
}

#[derive(Debug, Clone, Copy)]
pub struct NalHeader {
    nal_unit_type: UnitType,
    nal_ref_idc: u8,
}

#[derive(Debug, Clone, Copy)]
pub enum NalHeaderError {
    NalRefIdcOutOfRange(u8),
    InvalidNalRefIdcForNalUnitType {
        nal_unit_type: UnitType,
        nal_ref_idc: u8,
    },
    NalUnitTypeOutOfRange(UnitType),
}

impl NalHeader {
    pub fn from_nal_unit_type_and_nal_ref_idc(
        nal_unit_type: UnitType,
        nal_ref_idc: u8,
    ) -> Result<NalHeader, NalHeaderError> {
        if nal_ref_idc >= 4 {
            return Err(NalHeaderError::NalRefIdcOutOfRange(nal_ref_idc));
        }
        match nal_unit_type.id() {
            0 => Err(NalHeaderError::NalUnitTypeOutOfRange(nal_unit_type)),
            6 | 9 | 10 | 11 | 12 => {
                if nal_ref_idc == 0 {
                    Ok(NalHeader {
                        nal_unit_type,
                        nal_ref_idc,
                    })
                } else {
                    Err(NalHeaderError::InvalidNalRefIdcForNalUnitType {
                        nal_unit_type,
                        nal_ref_idc,
                    })
                }
            }
            5 => {
                if nal_ref_idc != 0 {
                    Ok(NalHeader {
                        nal_unit_type,
                        nal_ref_idc,
                    })
                } else {
                    Err(NalHeaderError::InvalidNalRefIdcForNalUnitType {
                        nal_unit_type,
                        nal_ref_idc,
                    })
                }
            }
            32.. => Err(NalHeaderError::NalUnitTypeOutOfRange(nal_unit_type)),
            _ => Ok(NalHeader {
                nal_unit_type,
                nal_ref_idc,
            }),
        }
    }

    fn as_header_byte(&self) -> u8 {
        self.nal_ref_idc << 5 | self.nal_unit_type.id()
    }
}

pub struct NalUnitWriter<W: ?Sized + Write> {
    inner: W,
}

pub trait NalUnitWrite<W: ?Sized + Write> {
    type Writer: RbspWrite<W>;
    fn write_nal_header(self, nal_header: NalHeader) -> Result<Self::Writer>;
}

impl<W: Write> NalUnitWriter<W> {
    fn new(inner: W) -> Self {
        Self { inner }
    }
}

impl<W: Write> NalUnitWrite<W> for NalUnitWriter<W> {
    type Writer = RbspWriter<W>;

    fn write_nal_header(mut self, nal_header: NalHeader) -> Result<RbspWriter<W>> {
        self.inner.write_u8(nal_header.as_header_byte())?;
        Ok(RbspWriter::new(self.inner))
    }
}

pub struct RbspWriter<W: ?Sized + Write> {
    last_written: VecDeque<u8>,
    inner: W,
}

pub trait RbspWrite<W: ?Sized + Write> {
    type Writer: H264ByteStreamWrite<W>;
    fn finish_rbsp(self) -> Result<Self::Writer>;
}

impl<W: Write> RbspWriter<W> {
    pub fn new(inner: W) -> Self {
        Self {
            last_written: VecDeque::with_capacity(3),
            inner,
        }
    }
}

impl<W: Write> RbspWrite<W> for RbspWriter<W> {
    type Writer = W;
    fn finish_rbsp(mut self) -> Result<W> {
        self.write_u8(0x80)?;
        Ok(self.inner)
    }
}

impl<W: ?Sized + Write> Write for RbspWriter<W> {
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        let mut written = 0;
        for &byte in buf {
            let mut last_written_iter = self.last_written.iter();
            if last_written_iter.next() == Some(&0)
                && last_written_iter.next() == Some(&0)
                && (byte == 0 || byte == 1 || byte == 2 || byte == 3)
            {
                self.inner.write_u8(3)?;
                self.last_written.clear();
            }
            self.inner.write_u8(byte)?;
            written += 1;
            self.last_written.push_back(byte);
            if self.last_written.len() > 2 {
                self.last_written.pop_front();
            }
        }
        Ok(written)
    }

    fn flush(&mut self) -> Result<()> {
        self.inner.flush()
    }
}

pub(crate) struct CountingSink {
    count: usize,
}

impl CountingSink {
    pub fn new() -> Self {
        Self { count: 0 }
    }

    pub fn count(&self) -> usize {
        self.count
    }
}

impl Write for CountingSink {
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        self.count += buf.len();
        Ok(buf.len())
    }

    fn flush(&mut self) -> Result<()> {
        Ok(())
    }
}

pub(crate) fn write_sei_header<W: ?Sized + Write>(
    writer: &mut W,
    mut payload_type: usize,
    mut payload_size: usize,
) -> std::io::Result<()> {
    while payload_type >= 255 {
        writer.write_u8(255)?;
        payload_type -= 255;
    }
    writer.write_u8(payload_type.try_into().unwrap())?;
    while payload_size >= 255 {
        writer.write_u8(255)?;
        payload_size -= 255;
    }
    writer.write_u8(payload_size.try_into().unwrap())?;
    Ok(())
}

impl<W: Write + ?Sized> WebvttWrite for RbspWriter<W> {
    fn write_webvtt_header(
        &mut self,
        max_latency_to_video: Duration,
        send_frequency_hz: u8,
        subtitle_tracks: &[WebvttTrack],
    ) -> std::io::Result<()> {
        write_webvtt_header(
            self,
            max_latency_to_video,
            send_frequency_hz,
            subtitle_tracks,
        )
    }

    fn write_webvtt_payload(
        &mut self,
        track_index: u8,
        chunk_number: u64,
        chunk_version: u8,
        video_offset: Duration,
        webvtt_payload: &str, // TODO: replace with string type that checks for interior NULs
    ) -> std::io::Result<()> {
        write_webvtt_payload(
            self,
            track_index,
            chunk_number,
            chunk_version,
            video_offset,
            webvtt_payload,
        )
    }
}

#[cfg(test)]
mod tests {
    use crate::{
        h264::{NalHeader, NalUnitWrite, NalUnitWriter, RbspWrite},
        webvtt::{WebvttWrite, PAYLOAD_GUID, USER_DATA_UNREGISTERED},
    };
    use byteorder::{BigEndian, ReadBytesExt};
    use h264_reader::nal::{Nal, RefNal, UnitType};
    use std::{io::Read, time::Duration};

    #[test]
    fn check_webvtt_sei() {
        let mut writer = vec![];

        let nalu_writer = NalUnitWriter::new(&mut writer);
        let nal_unit_type = h264_reader::nal::UnitType::SEI;
        let nal_ref_idc = 0;
        let nal_header =
            NalHeader::from_nal_unit_type_and_nal_ref_idc(nal_unit_type, nal_ref_idc).unwrap();
        let mut payload_writer = nalu_writer.write_nal_header(nal_header).unwrap();
        let track_index = 0;
        let chunk_number = 1;
        let chunk_version = 0;
        let video_offset = Duration::from_millis(200);
        let webvtt_payload = "Some unverified data";
        payload_writer
            .write_webvtt_payload(
                track_index,
                chunk_number,
                chunk_version,
                video_offset,
                webvtt_payload,
            )
            .unwrap();
        payload_writer.finish_rbsp().unwrap();
        assert!(&writer[3..19] == PAYLOAD_GUID.as_bytes());

        let nal = RefNal::new(&writer, &[], true);
        assert!(nal.is_complete());
        assert!(nal.header().unwrap().nal_unit_type() == UnitType::SEI);
        let mut byte_reader = nal.rbsp_bytes();

        assert!(usize::from(byte_reader.read_u8().unwrap()) == USER_DATA_UNREGISTERED);
        let mut length = 0;
        loop {
            let byte = byte_reader.read_u8().unwrap();
            length += usize::from(byte);
            if byte != 255 {
                break;
            }
        }
        assert!(length + 1 == byte_reader.clone().bytes().count());
        byte_reader.read_u128::<BigEndian>().unwrap();
        assert!(track_index == byte_reader.read_u8().unwrap());
        assert!(chunk_number == byte_reader.read_u64::<BigEndian>().unwrap());
        assert!(chunk_version == byte_reader.read_u8().unwrap());
        assert!(
            u16::try_from(video_offset.as_millis()).unwrap()
                == byte_reader.read_u16::<BigEndian>().unwrap()
        );
        println!("{writer:02x?}");
    }

    #[test]
    fn check_webvtt_multi_sei() {
        let mut writer = vec![];

        let nalu_writer = NalUnitWriter::new(&mut writer);
        let nal_unit_type = h264_reader::nal::UnitType::SEI;
        let nal_ref_idc = 0;
        let nal_header =
            NalHeader::from_nal_unit_type_and_nal_ref_idc(nal_unit_type, nal_ref_idc).unwrap();
        let mut payload_writer = nalu_writer.write_nal_header(nal_header).unwrap();
        let track_index = 0;
        let chunk_number = 1;
        let chunk_version = 0;
        let video_offset = Duration::from_millis(200);
        let webvtt_payload = "Some unverified data";
        payload_writer
            .write_webvtt_payload(
                track_index,
                chunk_number,
                chunk_version,
                video_offset,
                webvtt_payload,
            )
            .unwrap();
        payload_writer
            .write_webvtt_payload(1, 1, 0, video_offset, "Something else")
            .unwrap();
        payload_writer.finish_rbsp().unwrap();
        assert!(&writer[3..19] == PAYLOAD_GUID.as_bytes());

        let nal = RefNal::new(&writer, &[], true);
        assert!(nal.is_complete());
        assert!(nal.header().unwrap().nal_unit_type() == UnitType::SEI);
        let mut byte_reader = nal.rbsp_bytes();

        assert!(usize::from(byte_reader.read_u8().unwrap()) == USER_DATA_UNREGISTERED);
        let mut _length = 0;
        loop {
            let byte = byte_reader.read_u8().unwrap();
            _length += usize::from(byte);
            if byte != 255 {
                break;
            }
        }
        byte_reader.read_u128::<BigEndian>().unwrap();
        assert!(track_index == byte_reader.read_u8().unwrap());
        assert!(chunk_number == byte_reader.read_u64::<BigEndian>().unwrap());
        assert!(chunk_version == byte_reader.read_u8().unwrap());
        assert!(
            u16::try_from(video_offset.as_millis()).unwrap()
                == byte_reader.read_u16::<BigEndian>().unwrap()
        );
        println!("{writer:02x?}");
    }
}
