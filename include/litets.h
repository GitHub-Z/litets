#ifndef __LITETS_TSSTREAM_H__
#define __LITETS_TSSTREAM_H__

#ifdef WIN32
#include <basetsd.h>
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
#define __LITTLE_ENDIAN		1
#define __BIG_ENDIAN		2
#define __BYTE_ORDER		1
#else
#include <inttypes.h>
#include <endian.h>
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#include "streamdef_le.h"
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Temporarily not support big-endian systems."
#else
#error "Please fix <endian.h>"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************/
/* �ӿ���ؽṹ��                                                       */
/************************************************************************/
#define EsFrame_Unknown	((uint8_t)0x00)
#define EsFrame_MP3		((uint8_t)0x03)
#define EsFrame_MP2		((uint8_t)0x04)
#define EsFrame_MPEG4	((uint8_t)0x10)
#define EsFrame_H264	((uint8_t)0x1B)

#define IS_VIDEO(type)	(((uint8_t)type == EsFrame_MPEG4 || (uint8_t)type == EsFrame_H264) ? 1 : 0)

typedef void (*SEGCALLBACK)(uint8_t *buf, int len, void *ctx);

#define MIN_PES_LENGTH	(1000)
#define MAX_PES_LENGTH	(65000)

typedef struct
{
	int program_number; // ��Ŀ��ţ�����TsProgramInfo��prog�����±꣬����PS��ֵֻ��Ϊ0
	int stream_number;	// ����ţ�����TsProgramSpec��stream�����±�
	uint8_t *frame;		// ֡����
	int length;			// ֡����
	int is_key;			// ��ǰ֡TS����ʱ�Ƿ��PAT��PMT
	uint64_t pts;		// ʱ���, 90kHz
	int ps_pes_length;	// ��Ҫ�зֳ�PES�ĳ��ȣ��ò���ֻ��PS��Ч������ܳ���MAX_PES_LENGTH
	SEGCALLBACK segcb;	// ����PS��������һ�����ݣ�ͷ����PES���ص������ÿ���ΪNULL
	void *ctx;			// �ص�������
} TEsFrame;

/************************************************************************/
/* Ŀǰ���֧��1����Ŀ2����                                             */
/************************************************************************/
// ÿ����������
typedef struct
{
	char type;				// [I]ý������
	char stream_id;			// [O]ʵ����ID����PESͷ��id��ͬ��PS�����ã�
	int es_pid;				// [O]ʵ������PID��TS�����ã�
	int continuity_counter;	// [O] TS��ͷ��������������, �ⲿ��Ҫά���������ֵ, ����ÿ�δ����ϴδ����ļ���ֵ
} TsStreamSpec;

// ÿ����Ŀ������
#define MAX_STREAM_NUM		(4)

typedef struct
{
	int stream_num;			// [I]�����Ŀ������������
	int key_stream_id;		// {I]��׼�����
	int pmt_pid;			// [O]�����Ŀ��Ӧ��PMT���PID��TS�����ã�
	int mux_rate;			// [O]�����Ŀ�����ʣ���λΪ50�ֽ�ÿ��(PS������)
	TsStreamSpec stream[MAX_STREAM_NUM];
} TsProgramSpec;

// ��Ŀ��Ϣ
#define MAX_PROGRAM_NUM		(1)
typedef struct
{
	int program_num;		// [I]���TS�������Ľ�Ŀ����������PS��ֵֻ��Ϊ1
	int pat_pmt_counter;	// [O]PAT��PMT������
	TsProgramSpec prog[MAX_PROGRAM_NUM];
} TsProgramInfo;

/************************************************************************/
/* �ӿں�������֡����������ָ��TsProgramInfo							*/
/************************************************************************/

// ����������TS�ĳ��ȣ�������dest�ռ䲻�㣩����-1�� 
int lts_ts_stream(TEsFrame *frame, uint8_t *dest, int maxlen, TsProgramInfo *pi);

// ����PS�ĳ��ȣ�������dest�ռ䲻�㣩����-1�� 
int lts_ps_stream(TEsFrame *frame, uint8_t *dest, int maxlen, TsProgramInfo *pi);

/************************************************************************/
/* �ӿں�������֡��                                                     */
/************************************************************************/
typedef struct  
{
	TsProgramInfo info;		// ��Ŀ��Ϣ
	int is_pes;				// �������ݣ�����PSI
	int program_no;			// ��ǰ�������Ľ�Ŀ��
	int stream_no;			// ��ǰ������������
	uint64_t pts;			// ��ǰ����ʱ���
	uint8_t *pack_ptr;		// ���һ�����׵�ַ
	int pack_len;			// ���һ���ĳ���
	uint8_t *es_ptr;		// ES�����׵�ַ
	int es_len;				// ES���ݳ���
	int pes_head_len;		// PESͷ������
	int sync_only;			// ֻͬ��������������
	int ps_started;			// ���ҵ�PSͷ��
} TDemux;

// TS�����⸴�ã��ɹ������Ѵ����ȣ�ʧ�ܷ���-1
// ts_buf�Ǵ����TS���Ļ���
int lts_ts_demux(TDemux *handle, uint8_t *ts_buf, int len);

// PS�����⸴�ã��ɹ������Ѵ����ȣ�ʧ�ܷ���-1
// ps_buf�Ǵ����PS���Ļ���
int lts_ps_demux(TDemux *handle, uint8_t *ps_buf, int len);

/************************************************************************/
/* ���洦�����ӿ�                                                     */
/************************************************************************/
typedef struct
{
	int buf_size;
	int (*input)(uint8_t *buf, int size, void *context);
	int (*output)(uint8_t *buf, int size, void *context);
	void *context;
} TBufferHandler;

int lts_buffer_handle(TBufferHandler *handler);

#ifdef __cplusplus
}
#endif

#endif //__LITETS_TSSTREAM_H__
