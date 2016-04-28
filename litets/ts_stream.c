#include "litets.h"
#include <stdio.h>
#include <memory.h>
#include <stdio.h>
#define TS_STREAM_ID		(0x0010)
#define TS_PMT_ID_BASE		(0x1000)

// ȷ��Ψһ��PMT PID
static uint16_t get_ts_pmt_id(int program_number)
{
	return TS_PMT_ID_BASE + (uint16_t)program_number;
}

// ȷ��Ψһ��ý����PID
static uint16_t get_ts_stream_id(int program_number, int stream_number)
{
	return 0x0100 + program_number * MAX_STREAM_NUM + stream_number;
}

// ȷ��PES�е�stream_id
static uint8_t get_pes_stream_id(int type, int program_number, int stream_number)
{
	uint8_t id = 0;

	if (IS_VIDEO(type))
		id = 0xE0 + program_number * MAX_STREAM_NUM + stream_number;
	else
		id = 0xC0 + program_number * MAX_STREAM_NUM + stream_number;

	return id;
}

// CRC32
static uint32_t CRC_encode(uint8_t* data, int len)
{
#define CRC_poly_32 0x04c11db7

	int byte_count=0, bit_count=0;
	uint32_t CRC = 0xffffffff;

	while (byte_count < len)
	{
		if (((uint8_t)(CRC>>31)^(*data>>(7-bit_count++)&1)) != 0)
			CRC = CRC << 1^CRC_poly_32;
		else 
			CRC = CRC << 1;

		if (bit_count > 7)
		{
			bit_count = 0;
			byte_count++;
			data++;
		}
	}

	return CRC;
}

// ����PESͷ�е�PTS��DTS�ֶ�
static void make_pes_pts_dts(uint8_t *buff, uint64_t ts)
{
	// PTS
	buff[0] = (uint8_t)(((ts >> 30) & 0x07) << 1) | 0x30 | 0x01;
	buff[1] = (uint8_t)((ts >> 22) & 0xff);
	buff[2] = (uint8_t)(((ts >> 15) & 0xff) << 1) | 0x01;
	buff[3] = (uint8_t)((ts >> 7) & 0xff);
	buff[4] = (uint8_t)((ts & 0xff) << 1) | 0x01;
	// DTS
	buff[5] = (uint8_t)(((ts >> 30) & 0x07) << 1) | 0x10 | 0x01;
	buff[6] = (uint8_t)((ts >> 22) & 0xff);
	buff[7] = (uint8_t)(((ts >> 15) & 0xff) << 1) | 0x01;
	buff[8] = (uint8_t)((ts >> 7) & 0xff);
	buff[9] = (uint8_t)((ts & 0xff) << 1) | 0x01;
}

// ����TSͷ������Ӧ����PCR�ֶ�
static void make_ts_pcr(char *buff, uint64_t ts)
{
	buff[0] = (uint8_t)(ts >> 25);
	buff[1] = (uint8_t)(ts >> 17);
	buff[2] = (uint8_t)(ts >> 9);
	buff[3] = (uint8_t)(ts >> 1);
	buff[4] = (uint8_t)(((ts & 1) << 7) | 0x7e);
	buff[5] = 0x00;
}

// ����һ����PTS��DTS��PESͷ��
// Ŀǰ��pes_packet_length����2B, ����Ϊ0, ����TS, ��׼��������Ϊ0
static int gen_pes_head_with_pts_dts(TEsFrame *frame, uint8_t *dst, TsProgramInfo *pi)
{
	uint8_t *buf = dst;
	pes_head_flags flags = {0};
	int type;
	int pno = frame->program_number;
	int sno = frame->stream_number;
	int pes_packet_length = frame->length + 13;

	if (pes_packet_length > 65535)
	{
		pes_packet_length = 0;
	}

	// prefix
	*buf++ = 0;
	*buf++ = 0;
	*buf++ = 1;

	// check
	if (pno >= pi->program_num)
	{
		return -1;
	}
	if (sno >= pi->prog[pno].stream_num)
	{
		return -1;
	}

	// stream_id
	type = pi->prog[pno].stream[sno].type;
	*buf++ = get_pes_stream_id(type, pno, sno);

	// pes length
	*buf++ = (uint8_t)(pes_packet_length >> 8);
	*buf++ = (uint8_t)pes_packet_length;

	// pes head flags
	flags.reserved = 2;
	flags.PTS_DTS_flags = 3;
	memcpy(buf, &flags, 2);
	buf += 2;

	// pes head length
	*buf++ = 10;

	// PTS & DTS
	make_pes_pts_dts(buf, frame->pts);

	return 19;
}

// ����һ����׼��TSͷ��
// Ŀǰ��ʵ��, ����adaptation_field
static int gen_ts_packet_header(char *pkt, uint16_t PID, int payload_len)
{
	ts_header *header = (ts_header *)pkt;

	header->head.sync_byte = 0x47;
	header->head.transport_error_indicator = 0;
	header->head.payload_unit_start_indicator = 1;			// Ĭ�Ϻ���һ��
	header->head.transport_priority = 0;
	header->head.PID_high5 = (uint8_t)(PID >> 8) & 0x1f;
	header->head.PID_low8 = (uint8_t)PID;
	header->head.transport_scrambling_control = 0;
	header->head.adaptation_field_control = 3;				// �̶�Ϊ3, ��ʾ������Ӧ���͸���
	header->head.continuity_counter = 0;

	// ����Ӧ�����ȸ���ʵ�ʵĸ��س��ȵ���, ʹ������188���ֽ�
	header->adaptation_field_length = 188 - 4 - 1 - payload_len;

	header->flags.discontinuity_indicator = 0;
	header->flags.random_access_indicator = 0;
	header->flags.elementary_stream_priority_indicator = 0;
	header->flags.PCR_flag = 0;								// Ĭ����PCR, ����ʹ��ʱ������Ҫ����
	header->flags.OPCR_flag = 0;
	header->flags.splicing_point_flag = 0;
	header->flags.transport_private_data_flag = 0;
	header->flags.adaptation_field_extension_flag = 0;

	// padding
	memset(pkt + 6, 0xff, header->adaptation_field_length - 1);

	return 188 - payload_len;
}

//����TS������������
static void set_ts_header_counter(char* pkt, int counter)
{
	ts_header* header = (ts_header*)pkt;
	header->head.continuity_counter = counter & 0x0f;
}
// ����һ��PAT��TS��
static void gen_pat_ts_packet(char *pkt, TsProgramInfo *pi)
{
	// TS��IDĿǰȡ�̶�ֵ
	uint16_t PID = TS_STREAM_ID;

	// ����PAT����
	int pat_len = sizeof(pat_section) + pi->program_num * sizeof(pat_map_array) + 4;
	int pat_section_len = 5 + pi->program_num * sizeof(pat_map_array) + 4;

	// ����TSͷ���ҵ�PAT��ʼд��λ�ã�PSI��TSPIDΪ0
	int start_pos = gen_ts_packet_header(pkt, 0, pat_len + 1);
	
	char *pointer_field;
	pat_section *pat;
	pat_map_array *map;
	uint8_t *crc32ch;
	uint32_t crc32;
	int i;
	
	set_ts_header_counter(pkt, pi->pat_pmt_counter);
	// pointer_field
	pointer_field = (char *)&pkt[start_pos];
	start_pos += 1;
	*pointer_field = 0;

	// pat section
	pat = (pat_section *)&pkt[start_pos];
	start_pos += sizeof(pat_section);
	pat->table_id = 0x00;
	pat->section_syntax_indicator = 1;
	pat->zero = 0;
	pat->reserved1 = 3;
	pat->section_length_high4 = (uint8_t)(pat_section_len >> 8) & 0x0f;
	pat->section_length_low8 = (uint8_t)pat_section_len;
	pat->transport_stream_id_high8 = (uint8_t)(PID >> 8);
	pat->transport_stream_id_low8 = (uint8_t)PID;
	pat->reserved2 = 3;
	pat->version_number = 0;
	pat->current_next_indicator = 1;
	pat->section_number = 0x00;
	pat->last_section_number = 0x00;

	// pat program map arrays
	map = (pat_map_array *)&pkt[start_pos];
	start_pos += pi->program_num * sizeof(pat_map_array);
	for (i = 0; i < pi->program_num; i++)
	{
		int program_number = i + 1;
		// PMT-PID����������PMT��һ��
		int pmt_id = get_ts_pmt_id(program_number);
		map[i].program_number_high8 = (uint8_t)(program_number >> 8);
		map[i].program_number_low8 = (uint8_t)program_number;
		map[i].reserved = 7;
		map[i].program_map_PID_high5 = (uint8_t)(pmt_id >> 8) & 0x1f;
		map[i].program_map_PID_low8 = (uint8_t)pmt_id;
	}

	// crc32
	crc32ch = (uint8_t *)&pkt[start_pos];
	start_pos += 4;
	crc32 = CRC_encode((uint8_t *)pat, pat_len - 4);
	crc32ch[0] = (uint8_t)(crc32 >> 24);
	crc32ch[1] = (uint8_t)(crc32 >> 16);
	crc32ch[2] = (uint8_t)(crc32 >> 8);
	crc32ch[3] = (uint8_t)crc32;
}

// ����һ��PMT��TS��
static void gen_pmt_ts_packet(char *pkt, int pno, TsProgramSpec *ps)
{
	// TS-PIDȡPMT-PID, ������PAT�е�PMT-PIDһ��
	int program_number = pno + 1;
	uint16_t PID = get_ts_pmt_id(program_number);

	// ����PMT����
	int pmt_len = sizeof(pmt_section) + ps->stream_num * sizeof(pmt_stream_array) + 4;
	int pmt_section_len = 9 + ps->stream_num * sizeof(pmt_stream_array) + 4;

	// ����TSͷ���ҵ�PMT��ʼд��λ��
	int start_pos = gen_ts_packet_header(pkt, PID, pmt_len + 1);

	char *pointer_field;
	pmt_section *pmt;
	pmt_stream_array *strm;
	uint8_t *crc32ch;
	uint32_t crc32;
	int i;
	uint16_t PCR_PID = get_ts_stream_id(pno, 0); // Ĭ�ϻ�׼��ID

	// �趨��׼��ID
	if (0 <= ps->key_stream_id && ps->key_stream_id < ps->stream_num)
		PCR_PID = get_ts_stream_id(pno, ps->key_stream_id);
	
	// pointer_field
	pointer_field = (char *)&pkt[start_pos];
	start_pos += 1;
	*pointer_field = 0;
	
	// pmt section
	pmt = (pmt_section *)&pkt[start_pos];
	start_pos += sizeof(pmt_section);
	pmt->table_id = 0x02;
	pmt->section_syntax_indicator = 1;
	pmt->zero = 0;
	pmt->reserved1 = 3;
	pmt->section_length_high4 = (uint8_t)(pmt_section_len >> 8) & 0x0f;
	pmt->section_length_low8 = (uint8_t)pmt_section_len;
	pmt->program_number_high8 = (uint8_t)(program_number >> 8);
	pmt->program_number_low8 = (uint8_t)program_number;
	pmt->reserved2 = 3;
	pmt->version_number = 0;
	pmt->current_next_indicator = 1;
	pmt->section_number = 0x00;
	pmt->last_section_number = 0x00;
	pmt->reserved3 = 7;
	pmt->PCR_PID_high5 = (uint8_t)(PCR_PID >> 8) & 0x1f;
	pmt->PCR_PID_low8 = (uint8_t)PCR_PID;
	pmt->reserved4 = 15;
	pmt->program_info_length_high4 = 0;
	pmt->program_info_length_low8 = 0;
	
	// pmt program map arrays
	strm = (pmt_stream_array *)&pkt[start_pos];
	start_pos += ps->stream_num * sizeof(pmt_stream_array);
	for (i = 0; i < ps->stream_num; i++)
	{
		// ʵ����PID���������ʵ������TS-PID��ͬ
		uint16_t elementary_PID = get_ts_stream_id(pno, i);
		strm[i].stream_type = ps->stream[i].type;
		strm[i].reserved1 = 7;
		strm[i].elementary_PID_high5 = (uint8_t)(elementary_PID >> 8) & 0x1f;
		strm[i].elementary_PID_low8 = (uint8_t)elementary_PID;
		strm[i].reserved2 = 15;
		strm[i].ES_info_length_high4 = 0;
		strm[i].ES_info_length_low8 = 0;
	}
	
	// crc32
	crc32ch = (uint8_t *)&pkt[start_pos];
	start_pos += 4;
	crc32 = CRC_encode((uint8_t *)pmt, pmt_len - 4);
	crc32ch[0] = (uint8_t)(crc32 >> 24);
	crc32ch[1] = (uint8_t)(crc32 >> 16);
	crc32ch[2] = (uint8_t)(crc32 >> 8);
	crc32ch[3] = (uint8_t)crc32;
}

// ����PCR, ͬʱpayload_unit_start_indicator����Ϊ1
static void set_ts_header_with_pcr(char *pkt, uint64_t ts)
{
	ts_header *header = (ts_header *)pkt;
	header->head.payload_unit_start_indicator = 1;
	header->flags.PCR_flag = 1;
	make_ts_pcr(pkt + 6, ts);
}

// ���ò���PCR, ͬʱpayload_unit_start_indicator����Ϊ0
static void set_ts_header_without_pcr(char *pkt)
{
	ts_header *header = (ts_header *)pkt;
	header->head.payload_unit_start_indicator = 0;
}

// ��һ֡��������TS��, �������ɵ��ֽ���
static int gen_pes_ts_packets(TEsFrame *frame, char *dest, int maxlen, TsProgramInfo *pi)
{
	uint8_t pes_header[19];
	int pes_len;
	char *ptr = dest;
	int start_pos;
	// ʵ������TS-PID������PMT�е�һ��
	uint16_t PID = get_ts_stream_id(frame->program_number, frame->stream_number);
	int total_length = 0;
	TsStreamSpec *ss = &pi->prog[frame->program_number].stream[frame->stream_number];

	// pes header 19B
	gen_pes_head_with_pts_dts(frame, pes_header, pi);

	pes_len = 19 + frame->length;

	// ���Է���һ��TS����, ��PCR��TSͷ����Ϊ12B
	if (pes_len <= 176/*188-12*/)
	{
		total_length = 188;
		if (maxlen < total_length)
		{
			return -1;
		}

		start_pos = gen_ts_packet_header(ptr, PID, pes_len);
		set_ts_header_with_pcr(ptr, frame->pts);
		set_ts_header_counter(ptr, ss->continuity_counter++);//����������
		memcpy(ptr + start_pos, pes_header, 19);
		memcpy(ptr + start_pos + 19, frame->frame, frame->length);
	}
	// ���ܷ���һ����, ��Ҫ��ַ���ü���TS��
	else
	{
		int i;
		uint8_t *fp = frame->frame;
		int pes_tail_len = pes_len - 176;
		int pack_num = (pes_tail_len + 181) / 182; // 188 - 6

		total_length = 188 * (pack_num + 1);
		if (maxlen < total_length)
		{
			return -1;
		}

		// �ȷŵ�һ��, ��Ϊ��PCR, ����TSͷ����12B
		start_pos = gen_ts_packet_header(ptr, PID, 176);
		set_ts_header_with_pcr(ptr, frame->pts);
		set_ts_header_counter(ptr, ss->continuity_counter++);//����������
		memcpy(ptr + start_pos, pes_header, 19);
		memcpy(ptr + start_pos + 19, fp, 176 - 19);
		fp += (176 - 19);
		ptr += 188;

		// ���������, ����������PCR, ����TSͷ��ֻ��6B
		for (i = 0; i < pack_num; i++)
		{
			int payload_len = 182;
			if (i == pack_num - 1)
			{
				payload_len = pes_tail_len - i * 182;
			}

			start_pos = gen_ts_packet_header(ptr, PID, payload_len);
			set_ts_header_without_pcr(ptr);
			set_ts_header_counter(ptr, ss->continuity_counter++);//����������
			memcpy(ptr + start_pos, fp, payload_len);
			fp += payload_len;
			ptr += 188;
		}
	}

	return total_length;
}

/************************************************************************/
/* ����ӿ�, ��һ֡�����TS��, ����TS���ֽ���(�ض���188��������)        */
/************************************************************************/
// ע��: pi->continuity_counter�����Ҳ�ǳ���, ʹ������Ҫά���������ֵ
int lts_ts_stream(TEsFrame *frame, uint8_t *dest, int maxlen, TsProgramInfo *pi)
{
	int i;
	int ret;
	char *ptr = (char *)dest;
	int patpmt_len = 0;

	
	if (!frame || !dest || !maxlen || !pi)
	{
		return -1;
	}

	if (!frame->frame || !frame->length)
	{
		return -1;
	}

	// ���is_key==1, ��ô����PAT��PMT
	if (frame->is_key)
	{
		patpmt_len = 188 + 188 * pi->program_num;
		if (maxlen < patpmt_len)
		{
			return -1;
		}
		
		gen_pat_ts_packet(ptr, pi);
		ptr += 188;

		for (i = 0; i < pi->program_num; i++)
		{
			gen_pmt_ts_packet(ptr, i, &pi->prog[i]);
			set_ts_header_counter(ptr, pi->pat_pmt_counter);
			ptr += 188;
		}
		pi->pat_pmt_counter++;
	}

	// ����ʵ������TS��
	ret = gen_pes_ts_packets(frame, ptr, maxlen - patpmt_len, pi);
	if (ret <= 0)
	{
		return ret;
	}

	return patpmt_len + ret;
}
