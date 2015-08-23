
#include "microphonealsa.h"
#include "smartbuffer.h"

using namespace WTC;
#define PCM_DEVICE "default"

static const size_t MIC_BLOCK_PCM = 160;	// recoding block size
static const size_t MAX_BLOCKS = 32


// ----------------------------------------------------------------------------
CMicrophoneAlsa::CMicrophoneAlsa()
{
    m_tRecordingBlockSize = 0;

    m_bInClose = false;
    m_bIsOpen = false;
    m_pfnOnMicBufferCB = NULL;
    m_pvMicCBCtx = NULL;

	m_uiSampleRate = 8000;// (AudioCodec_PCM_Mono16bBigEndian8kHz) set in platform.h
	m_uiChannels = 1;// 1=mono, 2=stereo
	m_PcmHandle = NULL;
}


// ----------------------------------------------------------------------------
CMicrophoneAlsa::~CMicrophoneAlsa()
{
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::_internalOpenDevice(size_t *ptRCB, int *pBC)
{
	int err;
	int iDir = 0;
	uint32_t uiTmp;

	if (ptRCB)
	{
		*ptRCB = MIC_BLOCK_PCM; // * MIC_BLOCK_MULTIPLIER_PCM);
	}

	m_Frames = *ptRCB;

	// open device
	if ( (err = snd_pcm_open(&m_PcmHandle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0) ) < 0 )
	{
		fprintf(stderr, "cannot open audio device %s (%s)\n",
			PCM_DEVICE,
			snd_strerror(err));
		return false;
	}

	// Allocate parameters object and fill it with default values
	snd_pcm_hw_params_alloca(&m_params);

	// Initialize hardware param with full configuration
	if ( (err = snd_pcm_hw_params_any(m_PcmHandle, m_params)) < 0 )
	{
		fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
			snd_strerror(err));
		return false;
	}
	// Set  the interleaved read/write format
	if ( (err = snd_pcm_hw_params_set_access(m_PcmHandle, m_params, SND_PCM_ACCESS_RW_INTERLEAVED) ) < 0 )
	{
		fprintf(stderr, "cannot set access type (%s)\n",
			snd_strerror(err));
		return false;
	}

	// Set sample format "signed 16-bit PCM, Little Endian"
	if ( (err = snd_pcm_hw_params_set_format(m_PcmHandle, m_params, SND_PCM_FORMAT_S16_LE)) < 0 )
	{
		fprintf(stderr, "cannot set sample format (%s)\n",
			snd_strerror(err));
		return false;
	}

	// Set channels count (mono/stereo)
	if ( (err = snd_pcm_hw_params_set_channels(m_PcmHandle, m_params, m_uiChannels) ) < 0 )
	{
		fprintf(stderr, "cannot set channel count (%s)\n",
			snd_strerror(err));
		return false;
	}

	// Set sample rate
	if ( (err = snd_pcm_hw_params_set_rate_near(m_PcmHandle, m_params, &m_uiSampleRate, &iDir) ) < 0 )
	{
		fprintf(stderr, "cannot set sample rate (%s)\n",
			snd_strerror(err));
		return false;
	}

	// Set period size
	if ( (err = snd_pcm_hw_params_set_period_size_near(m_PcmHandle, m_params,  &m_Frames, &iDir) ) < 0 )
	{
		fprintf(stderr, "cannot set period time (%s)\n",
			snd_strerror(err));
		return false;
	}

        // Apply HW parameter settings to PCM device and prepare device
	if ( (err = snd_pcm_hw_params(m_PcmHandle, m_params) ) < 0 )
	{
		fprintf(stderr, "Error setting HW params (%s)\n",
			snd_strerror(err));
		return false;
	}

	// Set buffer large enouth to hold one period
	if ( (err = snd_pcm_hw_params_get_period_size(m_params, &m_Frames, &iDir) ) < 0 )
	{
		fprintf(stderr, "cannot get period size (%s)\n",
			snd_strerror(err));
		return false;
	}

	return true;
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::Create()
{
	m_bInClose = false;
	if ( !_internalOpenDevice(&m_tRecordingBlockSize, NULL) ) // &m_iBufferCount) )
	{
		return false;
	}
	return true;
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::_internalCreate()
{
	bool				rc;

	rc = false;

	if ( !_internalOpenDevice(&m_tRecordingBlockSize, NULL) ) // &m_iBufferCount) )
	{
		// TODO:(pv) Why does this sometimes fail on my HTC Touch Pro2 (WM6P)?
		goto end_function;
	}

	rc = true;

end_function:
	if ( !rc )
	{
		assert(0);
		Destroy();
	}

	return rc;
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::Destroy()
{
	m_bInClose = false;
	return _internalDestroy();
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::_internalDestroy()
{
	if ( !m_bInClose )
	{
		Close();
	}

	if ( m_PcmHandle )
	{
		while (TPSThreads::IsThread(&m_tMicThread) );
		snd_pcm_drain(m_PcmHandle);
		snd_pcm_close(m_PcmHandle);
		m_PcmHandle = NULL;
	}

	return true;
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::Open()
{
    bool				rc;

    if ( !_internalCreate() )
    {
        return false;
    }

    rc = false;

    if ( m_bIsOpen )
    {
        rc = true;
        goto end_function;
    }

	StartMicThread();
	m_bIsOpen = true;

    rc = true;

end_function:
    if ( !rc )
    {
        _WTCLogMsg(LOG_ERROR, "CMicrophoneWinMM::open rc = false; close();");
        assert(0);
        Close();
    }

    return rc;
}


// ----------------------------------------------------------------------------
bool CMicrophoneAlsa::Close()
{
	if ( !m_bIsOpen )
	{
		return true;
	}

	if ( !m_bInClose )
	{
		m_bInClose = true;
		StopMicThread();
		_internalDestroy();
		m_bInClose = false;
	}

	m_bIsOpen = false;

	return true;
}


// ----------------------------------------------------------------------------
void CMicrophoneAlsa::SetOnFrameCallback(PFN_ON_MIC_BUFFER_CB pFn, const void *pvCtx)
{
	m_pfnOnMicBufferCB = pFn;
	m_pvMicCBCtx = pvCtx;
}


// ----------------------------------------------------------------------------
void CMicrophoneAlsa::StartMicThread()
{
	TPSThreads::StartThread(&m_tMicThread, "CMicrophoneWinMM::callbackThread_Helper", CMicrophoneAlsa::MicThreadHelper, this);
	TPSThreads::SignalThreadAttention(&m_tMicThread);
}


// ----------------------------------------------------------------------------
void CMicrophoneAlsa::StopMicThread()
{
	TPSThreads::EasyStopThread(&m_tMicThread);
}


// ----------------------------------------------------------------------------
/*static*/ void CMicrophoneAlsa::MicThreadHelper(void *pv)
{
	((CMicrophoneAlsa*)pv)->MicThread();
}


// ----------------------------------------------------------------------------
void CMicrophoneAlsa::MicThread()
{
	uint8_t		ucBufferA[m_Frames*2];
	uint8_t		ucBufferB[m_Frames*2*MAX_BLOCKS];
	int			rc;
	unsigned int    ui=0;
	bool		bFaulted = false;

	while ( (TPSThreads::WaitForAttentionSignal(&m_tMicThread, 1) != TPSThreads::TPSTH_WAIT_RET_THREAD_MUST_STOP))
	{
		bFaulted = false;
		// Read audio from ALSA microphone device
		rc = snd_pcm_readi(m_PcmHandle, ucBufferA, m_Frames);
		if (rc == -EPIPE)
		{
			// EPIPE means overrun 
			_WTCLogMsg(LOG_ERROR, "snd_pcm_readi():: overrun occurred");
			snd_pcm_prepare(m_PcmHandle);
			bFaulted = true;
		}
		else if (rc < 0)
		{
			_WTCLogMsg(LOG_ERROR, "snd_pcm_readi():: read error");

			bFaulted = true;
		}
		else if (rc != (int)m_Frames)
		{
			_WTCLogMsg(LOG_ERROR, "snd_pcm_readi():: short read");
			bFaulted = true;
		}
		else
		{
			_WTCLogMsg(LOG_DEBUG, "snd_pcm_readi");
		}

		// Pass it on
		if (m_pfnOnMicBufferCB && !bFaulted)
		{
			memcpy(ucBufferB+ui*sizeof(ucBufferA),ucBufferA,sizeof(ucBufferA));
			ui++;
			if ((ui*m_Frames*2) % (MIC_BLOCK_PCM) == 0)
			{	
				m_pfnOnMicBufferCB(m_pvMicCBCtx, ucBufferB, ui*sizeof(ucBufferA));
				ui=0;
			}
			if (ui>=MAX_BLOCKS)
			{
				_WTCLogMsg(LOG_ERROR, "MicThread: ui %d >= %d (MAX_BLOCKS) -- Increase MAX_BLOCKS and recompile.", ui, MAX_BLOCKS);
				DebugBreak();
			}
		}
	}
}


