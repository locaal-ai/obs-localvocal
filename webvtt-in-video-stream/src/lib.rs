use std::{collections::VecDeque, sync::Mutex, time::Duration};
use video_bytestream_tools::webvtt::WebvttWrite;

pub struct WebvttMuxerBuilder {
    latency_to_video: Duration,
    send_frequency_hz: u8,
    video_frame_time: Duration,
    tracks: Vec<WebvttMuxerTrack>,
}

struct WebvttMuxerTrack {
    cues: VecDeque<WebvttCue>,
    default: bool,
    autoselect: bool,
    forced: bool,
    name: String,
    language: String,
    assoc_language: Option<String>,
    characteristics: Option<String>,
}

pub struct WebvttMuxer {
    latency_to_video: Duration,
    send_frequency_hz: u8,
    video_frame_time: Duration,
    inner: Mutex<WebvttMuxerInner>,
}

struct WebvttMuxerInner {
    tracks: Vec<WebvttMuxerTrack>,
    webvtt_buffer: String,
    next_chunk_number: u64,
    first_video_timestamp: Option<Duration>,
}

// TODO: this should probably be moved into video-bytestream-tools instead
pub struct WebvttString(String);

struct WebvttCue {
    start_time: Duration,
    duration: Duration,
    text: WebvttString,
}

pub struct NulError {
    pub string: String,
    pub nul_position: usize,
}

impl WebvttString {
    /// Create a `WebvttString`.
    /// This verifies that there are no interior NUL bytes, since
    /// the WebVTT-in-SEI wire format uses NUL terminated strings.
    ///
    /// # Errors
    ///
    /// This function will return an error if there are any NUL bytes in the string.
    pub fn from_string(string: String) -> Result<Self, NulError> {
        if let Some(nul_position) = string.find('\0') {
            Err(NulError {
                string,
                nul_position,
            })
        } else {
            Ok(WebvttString(string))
        }
    }
}

pub struct TooManySubtitleTracksError {
    pub name: WebvttString,
    pub language: WebvttString,
    pub assoc_language: Option<WebvttString>,
    pub characteristics: Option<WebvttString>,
}

impl WebvttMuxerBuilder {
    pub fn new(
        latency_to_video: Duration,
        send_frequency_hz: u8,
        video_frame_time: Duration,
    ) -> Self {
        Self {
            latency_to_video,
            send_frequency_hz,
            video_frame_time,
            tracks: vec![],
        }
    }

    // FIXME: split these arguments somehow?
    #[allow(clippy::too_many_arguments)]
    pub fn add_track(
        &mut self,
        default: bool,
        autoselect: bool,
        forced: bool,
        name: WebvttString,
        language: WebvttString,
        assoc_language: Option<WebvttString>,
        characteristics: Option<WebvttString>,
    ) -> Result<&mut Self, TooManySubtitleTracksError> {
        if self.tracks.len() == 0xff {
            return Err(TooManySubtitleTracksError {
                name,
                language,
                assoc_language,
                characteristics,
            });
        }
        self.tracks.push(WebvttMuxerTrack {
            cues: VecDeque::new(),
            default,
            autoselect,
            forced,
            name: name.0,
            language: language.0,
            assoc_language: assoc_language.map(|a| a.0),
            characteristics: characteristics.map(|c| c.0),
        });
        Ok(self)
    }

    pub fn create_muxer(self) -> WebvttMuxer {
        WebvttMuxer {
            latency_to_video: self.latency_to_video,
            send_frequency_hz: self.send_frequency_hz,
            video_frame_time: self.video_frame_time,
            inner: Mutex::new(WebvttMuxerInner {
                tracks: self.tracks,
                webvtt_buffer: String::new(),
                next_chunk_number: 0,
                first_video_timestamp: None,
            }),
        }
    }
}

pub struct InvalidWebvttTrack(pub u8);

impl WebvttMuxer {
    pub fn add_cue(
        &self,
        track: u8,
        start_time: Duration,
        duration: Duration,
        text: WebvttString,
    ) -> Result<(), InvalidWebvttTrack> {
        let mut inner = self.inner.lock().unwrap();
        let tracks = &mut inner.tracks;
        let track = tracks
            .get_mut(usize::from(track))
            .ok_or(InvalidWebvttTrack(track))?;
        let cues = &mut track.cues;
        let index = cues
            .iter()
            .position(|c| c.start_time > start_time)
            .unwrap_or(cues.len());
        cues.insert(
            index,
            WebvttCue {
                start_time,
                duration,
                text,
            },
        );
        Ok(())
    }

    fn consume_cues_into_chunk<'a>(
        cues: &mut VecDeque<WebvttCue>,
        timestamp: Duration,
        duration: Duration,
        buffer: &'a mut String,
    ) -> &'a str {
        while cues
            .front()
            .map(|cue| (cue.start_time + cue.duration) < timestamp)
            .unwrap_or(false)
        {
            cues.pop_front();
        }

        buffer.clear();

        for cue in &*cues {
            if cue.start_time > (timestamp + duration) {
                break;
            }
            let cue_start = if cue.start_time > timestamp {
                cue.start_time
            } else {
                timestamp
            };
            let cue_end = (cue.start_time + cue.duration).min(timestamp + duration);
            buffer.push_str(&format!(
                "{:0>2}:{:0>2}:{:0>2}.{:0>3} --> {:0>2}:{:0>2}:{:0>2}.{:0>3}\n{}\n\n",
                cue_start.as_secs() / 3600,
                cue_start.as_secs() % 3600 / 60,
                cue_start.as_secs() % 60,
                cue_start.as_millis() % 1000,
                cue_end.as_secs() / 3600,
                cue_end.as_secs() % 3600 / 60,
                cue_end.as_secs() % 60,
                cue_end.as_millis() % 1000,
                cue.text.0
            ))
        }
        buffer.as_str()
    }

    pub fn try_mux_into_bytestream(
        &self,
        video_timestamp: Duration,
        add_header: bool,
        writer: &mut impl WebvttWrite,
    ) -> std::io::Result<bool> {
        let mut inner = self.inner.lock().unwrap();
        let WebvttMuxerInner {
            tracks,
            webvtt_buffer,
            next_chunk_number,
            first_video_timestamp,
        } = &mut *inner;

        if add_header {
            // TODO: cache this? forward iter instead?
            let webvtt_tracks = tracks
                .iter()
                .map(|track| video_bytestream_tools::webvtt::WebvttTrack {
                    default: track.default,
                    autoselect: track.autoselect,
                    forced: track.forced,
                    language: &track.language,
                    name: &track.name,
                    assoc_language: track.assoc_language.as_deref(),
                    characteristics: track.characteristics.as_deref(),
                })
                .collect::<Vec<_>>();
            writer.write_webvtt_header(
                self.latency_to_video,
                self.send_frequency_hz,
                &webvtt_tracks,
            )?;
        }

        let duration_between_sends =
            Duration::from_secs_f64(1. / f64::from(self.send_frequency_hz));
        let first_video_timestamp = &*first_video_timestamp.get_or_insert(video_timestamp);
        let next_chunk_webvtt_timestamp =
            u32::try_from(*next_chunk_number).unwrap() * duration_between_sends;
        let next_chunk_video_timestamp =
            *first_video_timestamp + self.latency_to_video + next_chunk_webvtt_timestamp;
        if next_chunk_video_timestamp > video_timestamp + self.video_frame_time * 2 {
            return Ok(add_header);
        }
        let chunk_number = *next_chunk_number;
        // TODO: return an error type that allows skipping chunks if the writer fails?
        for (track_index, track) in tracks.iter_mut().enumerate() {
            let webvtt_payload = Self::consume_cues_into_chunk(
                &mut track.cues,
                next_chunk_webvtt_timestamp,
                duration_between_sends,
                webvtt_buffer,
            );
            writer.write_webvtt_payload(
                u8::try_from(track_index).unwrap(),
                chunk_number,
                0,
                video_timestamp - (*first_video_timestamp + next_chunk_webvtt_timestamp),
                webvtt_payload,
            )?;
        }
        *next_chunk_number += 1;
        Ok(true)
    }
}
