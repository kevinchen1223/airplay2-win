#include "pch.h"
#include "FgAirplayServer.h"
#include "Airplay2Head.h"


FgAirplayServer::FgAirplayServer()
	: m_pCallback(NULL)
	, m_pDnsSd(NULL)
	, m_pAirplay(NULL)
	, m_pRaop(NULL)
	, m_bVideoQuit(false)
	, m_pCodec(NULL)
	, m_pCodecCtx(NULL)
	, m_bCodecOpened(false)
{
	memset(&m_stAirplayCB, 0, sizeof(airplay_callbacks_t));
	memset(&m_stRaopCB, 0, sizeof(raop_callbacks_t));
	m_stAirplayCB.cls = this;
	m_stRaopCB.cls = this;

	m_stAirplayCB.audio_init = audio_init;
	m_stAirplayCB.audio_process = audio_process_ap;
	m_stAirplayCB.audio_flush = audio_flush;
	m_stAirplayCB.audio_destroy = audio_destroy;
	m_stAirplayCB.video_play = ap_video_play;
	m_stAirplayCB.video_get_play_info = ap_video_get_play_info;

	m_stRaopCB.connected = connected;
	m_stRaopCB.disconnected = disconnected;
	// m_stRaopCB.audio_init = audio_init;
	m_stRaopCB.audio_set_volume = audio_set_volume;
	m_stRaopCB.audio_set_metadata = audio_set_metadata;
	m_stRaopCB.audio_set_coverart = audio_set_coverart;
	m_stRaopCB.audio_process = audio_process;
	m_stRaopCB.audio_flush = audio_flush;
	// m_stRaopCB.audio_destroy = audio_destroy;
	m_stRaopCB.video_process = video_process;
}

FgAirplayServer::~FgAirplayServer()
{
	m_pCallback = NULL;
}

int FgAirplayServer::start(const char serverName[AIRPLAY_NAME_LEN], IAirServerCallback* callback)
{
	m_pCallback = callback;

	unsigned short raop_port = 5000;
	unsigned short airplay_port = 7000;
	const char hwaddr[] = { 0x48, 0x5d, 0x60, 0x7c, 0xee, 0x22 };
	char* pemstr = NULL;

	int ret = 0;
	do {
		m_pAirplay = airplay_init(10, &m_stAirplayCB, pemstr, &ret);
		if (m_pAirplay == NULL) {
			ret = -1;
			break;
		}
		ret = airplay_start(m_pAirplay, &airplay_port, hwaddr, sizeof(hwaddr), NULL);
		if (ret < 0) {
			break;
		}
		airplay_set_log_level(m_pAirplay, RAOP_LOG_DEBUG);
		airplay_set_log_callback(m_pAirplay, &log_callback, this);

		m_pRaop = raop_init(10, &m_stRaopCB);
		if (m_pRaop == NULL) {
			ret = -1;
			break;
		}

		raop_set_log_level(m_pRaop, RAOP_LOG_DEBUG);
		raop_set_log_callback(m_pRaop, &log_callback, this);
		ret = raop_start(m_pRaop, &raop_port);
		if (ret < 0) {
			break;
		}
		raop_set_port(m_pRaop, raop_port);

		m_pDnsSd = dnssd_init(&ret);
		if (m_pDnsSd == NULL) {
			ret = -1;
			break;
		}
		ret = dnssd_register_raop(m_pDnsSd, serverName, raop_port, hwaddr, sizeof(hwaddr), 0);
		if (ret < 0) {
			break;
		}
		ret = dnssd_register_airplay(m_pDnsSd, serverName, airplay_port, hwaddr, sizeof(hwaddr));
		if (ret < 0) {
			break;
		}

		raop_log_info(m_pRaop, "Startup complete... Kill with Ctrl+C\n");
	} while (false);

	if (ret != 0) {
		stop();
	}

	return 0;
}

void FgAirplayServer::stop()
{
	airplay_set_log_callback(m_pAirplay, &log_callback, NULL);
	raop_set_log_callback(m_pRaop, &log_callback, NULL);

	if (m_pDnsSd) {
		dnssd_unregister_airplay(m_pDnsSd);
		dnssd_unregister_raop(m_pDnsSd);
		dnssd_destroy(m_pDnsSd);
		m_pDnsSd = NULL;
	}

	if (m_pRaop) {
		raop_destroy(m_pRaop);
		m_pRaop = NULL;
	}

	if (m_pAirplay) {
		airplay_destroy(m_pAirplay);
		m_pAirplay = NULL;
	}

	unInitFFmpeg();
	m_pCallback = NULL;
}

void FgAirplayServer::connected(void* cls)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback != NULL)
	{
		pServer->m_pCallback->connected();
	}
}

void FgAirplayServer::disconnected(void* cls)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback != NULL)
	{
		pServer->m_pCallback->disconnected();
	}
}

void* FgAirplayServer::audio_init(void* opaque, int bits, int channels, int samplerate)
{
	return nullptr;
}

void FgAirplayServer::audio_set_volume(void* cls, void* session, float volume)
{
}

void FgAirplayServer::audio_set_metadata(void* cls, void* session, const void* buffer, int buflen)
{
}

void FgAirplayServer::audio_set_coverart(void* cls, void* session, const void* buffer, int buflen)
{
}

void FgAirplayServer::audio_process_ap(void* cls, void* session, const void* buffer, int buflen)
{
}

void FgAirplayServer::audio_process(void* cls, pcm_data_struct* data)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback != NULL)
	{
		SFgAudioFrame* frame = new SFgAudioFrame();
		frame->bitsPerSample = data->bits_per_sample;
		frame->channels = data->channels;
		frame->pts = data->pts;
		frame->sampleRate = data->sample_rate;
		frame->dataLen = data->data_len;
		frame->data = new uint8_t[frame->dataLen];
		memcpy(frame->data, data->data, frame->dataLen);

		pServer->m_pCallback->outputAudio(frame);
		delete[] frame->data;
		delete frame;
	}
}

void FgAirplayServer::audio_flush(void* cls, void* session)
{
}

void FgAirplayServer::audio_destroy(void* cls, void* session)
{
}

void FgAirplayServer::video_process(void* cls, h264_decode_struct* h264data)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (h264data->data_len <= 0)
	{
		return;
	}

	SFgH264Data* pData = new SFgH264Data();
	memset(pData, 0, sizeof(SFgH264Data));

	if (h264data->frame_type == 0)
	{
		pData->size = h264data->data_len;
		pData->data = new uint8_t[pData->size];
		pData->is_key = 1;
		memcpy(pData->data, h264data->data, h264data->data_len);
	}
	else if (h264data->frame_type == 1)
	{
		pData->size = h264data->data_len;
		pData->data = new uint8_t[pData->size];
		memcpy(pData->data, h264data->data, h264data->data_len);
	}

	pServer->decodeH264Data(pData);
	delete[] pData->data;
	delete pData;
}

void FgAirplayServer::ap_video_play(void* cls, char* url, double volume, double start_pos)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback)
	{
		pServer->m_pCallback->videoPlay(url, volume, start_pos);
	}
}

void FgAirplayServer::ap_video_get_play_info(void* cls, double* duration, double* position, double* rate)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer)
	{
		return;
	}
	if (pServer->m_pCallback)
	{
		pServer->m_pCallback->videoGetPlayInfo(duration, position, rate);
	}
}

void FgAirplayServer::log_callback(void* cls, int level, const char* msg)
{
	FgAirplayServer* pServer = (FgAirplayServer*)cls;
	if (!pServer) 
	{
		return;
	}
	if (pServer->m_pCallback)
	{
		pServer->m_pCallback->log(level, msg);
	}
}

int FgAirplayServer::initFFmpeg(const void* privatedata, int privatedatalen) {
	if (m_pCodec == NULL) {
		m_pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
		m_pCodecCtx = avcodec_alloc_context3(m_pCodec);
	}
	if (m_pCodec == NULL) {
		return -1;
	}

	m_pCodecCtx->extradata = (uint8_t*)av_malloc(privatedatalen);
	m_pCodecCtx->extradata_size = privatedatalen;
	memcpy(m_pCodecCtx->extradata, privatedata, privatedatalen);
	m_pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

	int res = avcodec_open2(m_pCodecCtx, m_pCodec, NULL);
	if (res < 0)
	{
		printf("Failed to initialize decoder\n");
		return -1;
	}

	m_bCodecOpened = true;
	this->m_bVideoQuit = false;

	return 0;
}

void FgAirplayServer::unInitFFmpeg()
{
	if (m_pCodecCtx)
	{
		if (m_pCodecCtx->extradata) {
			av_freep(&m_pCodecCtx->extradata);
		}
		avcodec_free_context(&m_pCodecCtx);
		m_pCodecCtx = NULL;
	}
}

int FgAirplayServer::decodeH264Data(SFgH264Data* data) {
	int ret = 0;
	if (!m_bCodecOpened && !data->is_key) {
		return 0;
	}
	if (data->is_key) {
		ret = initFFmpeg(data->data, data->size);
		if (ret < 0) {
			return ret;
		}
	}
	if (!m_bCodecOpened) {
		return 0;
	}

	AVPacket pkt1, * packet = &pkt1;
	int frameFinished;
	AVFrame* pFrame;

	pFrame = av_frame_alloc();

	av_new_packet(packet, data->size);
	memcpy(packet->data, data->data, data->size);

	ret = avcodec_send_packet(this->m_pCodecCtx, packet);
	frameFinished = avcodec_receive_frame(this->m_pCodecCtx, pFrame);

	av_packet_unref(packet);

	// Did we get a video frame?
	if (frameFinished == 0)
	{
		SFgVideoFrame* pVideoFrame = new SFgVideoFrame();
		pVideoFrame->width = pFrame->width;
		pVideoFrame->height = pFrame->height;
		pVideoFrame->pts = pFrame->pts;
		pVideoFrame->isKey = pFrame->key_frame;
		int ySize = pFrame->linesize[0] * pFrame->height;
		int uSize = pFrame->linesize[1] * pFrame->height >> 1;
		int vSize = pFrame->linesize[2] * pFrame->height >> 1;
		pVideoFrame->dataTotalLen = ySize + uSize + vSize;
		pVideoFrame->dataLen[0] = ySize;
		pVideoFrame->dataLen[1] = uSize;
		pVideoFrame->dataLen[2] = vSize;
		pVideoFrame->data = new uint8_t[pVideoFrame->dataTotalLen];
		memcpy(pVideoFrame->data, pFrame->data[0], ySize);
		memcpy(pVideoFrame->data + ySize, pFrame->data[1], uSize);
		memcpy(pVideoFrame->data + ySize + uSize, pFrame->data[2], vSize);
		pVideoFrame->pitch[0] = pFrame->linesize[0];
		pVideoFrame->pitch[1] = pFrame->linesize[1];
		pVideoFrame->pitch[2] = pFrame->linesize[2];

		if (m_pCallback != NULL)
		{
			m_pCallback->outputVideo(pVideoFrame);
		}
		delete[] pVideoFrame->data;
		delete pVideoFrame;
	}
	av_frame_free(&pFrame);

	return 0;
}
