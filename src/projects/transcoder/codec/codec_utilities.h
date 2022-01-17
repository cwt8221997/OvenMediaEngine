//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include "../transcoder_private.h"
#include "codec_base.h"

class TranscoderUtilities
{
public:
	static inline std::shared_ptr<MediaFrame> ConvertAvFrameToMediaFrame(cmn::MediaType media_type, AVFrame* frame)
	{
		switch (media_type)
		{
			case cmn::MediaType::Video: {
				auto video_frame = std::make_shared<MediaFrame>();

				video_frame->SetMediaType(media_type);
				video_frame->SetWidth(frame->width);
				video_frame->SetHeight(frame->height);
				video_frame->SetFormat(frame->format);
				video_frame->SetPts((frame->pts == AV_NOPTS_VALUE) ? -1LL : frame->pts);
				video_frame->SetDuration(frame->pkt_duration);

				int nb_plane = 0;
				for (int i = 0; i < AV_NUM_DATA_POINTERS; i++, nb_plane++)
				{
					if (frame->linesize[i] > 0)
					{
						video_frame->SetStride(frame->linesize[i], i);
					}
				}

				if (frame->format == AV_PIX_FMT_YUV444P)
				{
					video_frame->SetBuffer(frame->data[0], video_frame->GetStride(0) * video_frame->GetHeight(), 0);  // Y-Plane 4
					video_frame->SetBuffer(frame->data[1], video_frame->GetStride(1) * video_frame->GetHeight(), 1);  // Cb Plane 4
					video_frame->SetBuffer(frame->data[2], video_frame->GetStride(2) * video_frame->GetHeight(), 2);  // Cr Plane 4
				}
				else if (frame->format == AV_PIX_FMT_NV12 || frame->format == AV_PIX_FMT_NV21)
				{
					video_frame->SetBuffer(frame->data[0], video_frame->GetStride(0) * video_frame->GetHeight(), 0);	  // Y-Plane 4
					video_frame->SetBuffer(frame->data[1], video_frame->GetStride(1) * video_frame->GetHeight() / 2, 1);  // uv Plane 2
				}
				else if (frame->format == AV_PIX_FMT_YUV420P)
				{
					video_frame->SetBuffer(frame->data[0], video_frame->GetStride(0) * video_frame->GetHeight(), 0);	  // Y-Plane 4
					video_frame->SetBuffer(frame->data[1], video_frame->GetStride(1) * video_frame->GetHeight() / 2, 1);  // Cb Plane 2
					video_frame->SetBuffer(frame->data[2], video_frame->GetStride(2) * video_frame->GetHeight() / 2, 2);  // Cr Plane 2
				}
				else if (frame->format == AV_PIX_FMT_CUDA)
				{
					for (int i = 0; i < AV_NUM_DATA_POINTERS; i++, nb_plane++)
					{
						if (frame->linesize[i] > 0)
						{
							video_frame->SetBuffer(frame->data[i], video_frame->GetStride(i) * video_frame->GetHeight(), i);  // Y-Plane 4
						}
					}
				}
				else
				{
					video_frame->SetBuffer(frame->data[0], video_frame->GetStride(0) * video_frame->GetHeight(), 0);	  // Y-Plane 4
					video_frame->SetBuffer(frame->data[1], video_frame->GetStride(1) * video_frame->GetHeight() / 2, 1);  // Cb Plane 2
					video_frame->SetBuffer(frame->data[2], video_frame->GetStride(2) * video_frame->GetHeight() / 2, 2);  // Cr Plane 2
				}

				return video_frame;
			}
			case cmn::MediaType::Audio: {
				auto audio_frame = std::make_shared<MediaFrame>();

				audio_frame->SetMediaType(media_type);
				audio_frame->SetBytesPerSample(::av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)));
				audio_frame->SetNbSamples(frame->nb_samples);
				audio_frame->SetChannelCount(frame->channels);
				audio_frame->SetSampleRate(frame->sample_rate);
				audio_frame->SetFormat(frame->format);
				audio_frame->SetDuration(frame->pkt_duration);

				auto data_length = static_cast<uint32_t>(audio_frame->GetBytesPerSample() * audio_frame->GetNbSamples());

				// Copy frame data into out_buf
				if (IsPlanar(audio_frame->GetFormat<AVSampleFormat>()))
				{
					// If the frame is planar, the data is stored separately in the "_frame->data" array.
					for (int channel = 0; channel < frame->channels; channel++)
					{
						audio_frame->Resize(data_length, channel);
						uint8_t* output = audio_frame->GetWritableBuffer(channel);
						::memcpy(output, frame->data[channel], data_length);
					}
				}
				else
				{
					// If the frame is non-planar, it means interleaved data. So, just copy from "_frame->data[0]" into the output_frame
					audio_frame->AppendBuffer(frame->data[0], data_length * frame->channels, 0);
				}

				return audio_frame;
			}
			default:
				return nullptr;
				break;
		}

		return nullptr;
	}

	static inline int64_t GetDurationPerFrame(cmn::MediaType media_type, std::shared_ptr<TranscodeContext>& context, AVFrame* frame = nullptr)
	{
		switch (media_type)
		{
			case cmn::MediaType::Video: {
				// Calculate duration using framerate in timebase
				int den = context->GetTimeBase().GetDen();

				// TODO(soulk) : If there is no framerate value, the frame rate value cannot be calculated normally.
				int64_t duration = (den == 0) ? 0LL : (float)den / context->GetFrameRate();
				return duration;
			}
			break;
			case cmn::MediaType::Audio:
			default: {
				float frame_duration_in_second = frame->nb_samples * (1.0f / frame->sample_rate);
				int frame_duration_in_timebase = static_cast<int>(frame_duration_in_second * context->GetTimeBase().GetDen());
				return frame_duration_in_timebase;
			}
			break;
		}

		return -1;
	}

	static bool IsPlanar(AVSampleFormat format)
	{
		switch (format)
		{
			case AV_SAMPLE_FMT_U8:
			case AV_SAMPLE_FMT_S16:
			case AV_SAMPLE_FMT_S32:
			case AV_SAMPLE_FMT_FLT:
			case AV_SAMPLE_FMT_DBL:
			case AV_SAMPLE_FMT_S64:
				return false;

			case AV_SAMPLE_FMT_U8P:
			case AV_SAMPLE_FMT_S16P:
			case AV_SAMPLE_FMT_S32P:
			case AV_SAMPLE_FMT_FLTP:
			case AV_SAMPLE_FMT_DBLP:
			case AV_SAMPLE_FMT_S64P:
				return true;

			default:
				return false;
		}
	}

	static bool ConvertMediaFrameToAvFrame(cmn::MediaType media_type, std::shared_ptr<const MediaFrame> src, AVFrame* dst)
	{
		switch (media_type)
		{
			case cmn::MediaType::Video: {
				dst->format = src->GetFormat();
				dst->nb_samples = 1;
				dst->pts = src->GetPts();
				// The encoder will not pass this duration
				dst->pkt_duration = src->GetDuration();

				dst->width = src->GetWidth();
				dst->height = src->GetHeight();
				dst->linesize[0] = src->GetStride(0);
				dst->linesize[1] = src->GetStride(1);
				dst->linesize[2] = src->GetStride(2);

				if (::av_frame_get_buffer(dst, 32) < 0)
				{
					return false;
				}

				if (::av_frame_make_writable(dst) < 0)
				{
					return false;
				}

				::memcpy(dst->data[0], src->GetBuffer(0), src->GetBufferSize(0));
				::memcpy(dst->data[1], src->GetBuffer(1), src->GetBufferSize(1));
				::memcpy(dst->data[2], src->GetBuffer(2), src->GetBufferSize(2));
			}
			break;
			case cmn::MediaType::Audio: {
				dst->format = src->GetFormat();
				dst->nb_samples = src->GetNbSamples();
				
				dst->pts = src->GetPts();
				dst->pkt_duration = src->GetDuration();
				dst->channel_layout = (src->GetChannelCount() == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
				dst->channels = src->GetChannelCount();
				dst->sample_rate = src->GetSampleRate();

				if (::av_frame_get_buffer(dst, 0) < 0)
				{
					return false;
				}

				if (::av_frame_make_writable(dst) < 0)
				{
					return false;
				}

				::memcpy(dst->data[0], src->GetBuffer(0), src->GetBufferSize(0));
			}
			break;
			default: {
				return false;
			}
		}

		return true;
	}

	static std::shared_ptr<MediaPacket> ConvertAvPacketToMediaPacket(AVPacket* src, cmn::MediaType media_type, cmn::BitstreamFormat format, cmn::PacketType packet_type)
	{
		auto packet_buffer = std::make_shared<MediaPacket>(
			0,
			media_type,
			0,
			src->data,
			src->size,
			src->pts,
			src->dts,
			src->duration,
			(src->flags & AV_PKT_FLAG_KEY) ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag);

		if (packet_buffer == nullptr)
		{
			return nullptr;
		}

		packet_buffer->SetBitstreamFormat(format);
		packet_buffer->SetPacketType(packet_type);

		return packet_buffer;
	}
};
