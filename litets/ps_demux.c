#include "litets.h"
#include <stdio.h>

#define BUF2U16(buf)	(((buf)[0] << 8) | (buf)[1])

static int handle_header(TDemux *handle, uint8_t *buf, int len)
{
	int psize = 0;
	int staffing_len = 0;
	ps_pack_header *head = (ps_pack_header *)buf;

	if (len < sizeof(ps_pack_header))
	{
		return 0;
	}

	staffing_len = head->pack_stuffing_length & 0x07;
	psize = sizeof(ps_pack_header) + staffing_len;
	if (len < psize)
	{
		return 0;
	}

	// do somethings future...

	return psize;
}

static int handle_common_pack(uint8_t *buf, int len)
{
	int psize = 0;
	
	if (len < 6)
	{
		return 0;
	}
	
	psize = 6 + BUF2U16(&buf[4]);
	if (len < psize)
	{
		return 0;
	}
	
	return psize;
}

static int handle_system_header(TDemux *handle, uint8_t *buf, int len)
{
	int psize = handle_common_pack(buf, len);

	if (psize > 0)
	{
// 		ps_system_header *head = (ps_system_header *)buf;
		// do somethings future...
	}

	return psize;
}

static int handle_map(TDemux *handle, uint8_t *buf, int len)
{
	int psize = handle_common_pack(buf, len);
	
	if (psize > 0)
	{
		ps_map *head = (ps_map *)buf;
		int info_len = BUF2U16(head->ps_info_length);
		int map_len = BUF2U16(head->es_map_length + info_len);
		uint8_t *map_buf = buf + sizeof(ps_map) + info_len;
		int pos = 0;
		int i = 0;

		handle->info.program_num = 1;
		while (pos < map_len)
		{
			ps_map_es *mes = (ps_map_es *)(map_buf + pos);
			int es_info_len = BUF2U16(mes->es_info_length);
			if(es_info_len < 0)
				break;
			
			if (i >= MAX_STREAM_NUM)
			{
				break;
			}
			
			pos += (sizeof(ps_map_es) + es_info_len);

			// ���������Ƶ����Ƶ
			if ((mes->es_id & 0xC0) == 0xC0 || (mes->es_id & 0xE0) == 0xE0)
			{
				
				handle->info.prog[0].stream[i].type = mes->stream_type;
				handle->info.prog[0].stream[i].stream_id = mes->es_id;
				i++;
			}
		}
		handle->info.prog[0].stream_num = i;
	}
	
	return psize;
}

static int handle_finish(TDemux *handle, uint8_t *buf, int len)
{
	int psize = 4;
	return psize;
}

static int handle_pes(TDemux *handle, uint8_t *buf, int len)
{
	int psize = handle_common_pack(buf, len);
	
	if (psize > 0)
	{
		char stream_id = buf[3];

		// �ж��Ƿ���map���е�es
		if (handle->info.program_num == 1)
		{
			int i;
			for (i = 0; i < handle->info.prog[0].stream_num; i++)
			{
				if (stream_id == handle->info.prog[0].stream[i].stream_id)
				{
					// ��PES������д��Ŀ�ź�����
					handle->is_pes = 1;
					handle->program_no = 0;
					handle->stream_no = i;
					break;
				}
			}
		}

		// �ҵ�������PES��
		if (handle->is_pes)
		{
			// ����PTS
			{
				uint8_t flags_2 = buf[7];
				if (flags_2 & 0x80)
				{
					uint8_t *pts_buf = &buf[9];
					handle->pts  = ((uint64_t)pts_buf[0] & 0x0E) << 29;
					handle->pts |= ((uint64_t)pts_buf[1]       ) << 22;
					handle->pts |= ((uint64_t)pts_buf[2] & 0xFE) << 14;
					handle->pts |= ((uint64_t)pts_buf[3]       ) <<  7;
					handle->pts |= ((uint64_t)pts_buf[4] & 0xFE) >>  1;
					handle->pts /= 90;
				}
			}
			// ����ES
			{
				int pes_len = BUF2U16(&buf[4]);
				if (pes_len + 6 <= len)
				{
					handle->pes_head_len = buf[8];
					handle->es_ptr = buf + 9 + handle->pes_head_len;
					handle->es_len = pes_len - 3 - handle->pes_head_len;
				}
			}
		}

		// do somethings future...
	}
	
	return psize;
}

int lts_ps_demux(TDemux *handle, uint8_t *ps_buf, int len)
{
	int i = 0;
	int ret_len = 0;

	if (!handle || !ps_buf || len <= 0)
	{
		return -1;
	}

	handle->is_pes = 0;
	handle->program_no = 0;
	handle->stream_no = 0;
	handle->pts = 0;
	handle->pack_ptr = NULL;
	handle->pack_len = 0;
	handle->pes_head_len = 0;
	handle->es_ptr = NULL;
	handle->es_len = 0;

	for (i = 0; i < len - 4; i++)
	{
		ret_len = i;

		if (ps_buf[i] == 0 && ps_buf[i+1] == 0 && ps_buf[i+2] == 1)
		{
			int type = ps_buf[i+3];

/* 
PES stream id table:
stream_id	stream coding
1011 1100	program_stream_map
1011 1101	private_stream_1
1011 1110	padding_stream
1011 1111	private_stream_2
110x xxxx	ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC14496-3 audio stream number x xxxx
1110 xxxx	ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC14496-2 video stream number xxxx
1111 0000	ECM_stream
1111 0001	EMM_stream
1111 0010	ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A or ISO/IEC 13818-6_DSMCC_stream
1111 0011	ISO/IEC_13522_stream
1111 0100	ITU-T Rec. H.222.1 type A
1111 0101	ITU-T Rec. H.222.1 type B
1111 0110	ITU-T Rec. H.222.1 type C
1111 0111	ITU-T Rec. H.222.1 type D
1111 1000	ITU-T Rec. H.222.1 type E
1111 1001	ancillary_stream
1111 1010	ISO/IEC14496-1_SL-packetized_stream
1111 1011	ISO/IEC14496-1_FlexMux_stream
1111 11xx	reserved data stream
1111 1111	program_stream_directory
*/
			// PS stream_id�������0xB9
			if (type < 0xB9)
			{
				continue;
			}
			// �����û�ҵ�psͷ
			if (!handle->ps_started)
			{
				// �����һ������psͷ
				if (type != 0xBA)
				{
					// ��ô�Թ�������������Ѱ
					continue;
				}

				// ֱ���ҵ�psͷ
				handle->ps_started = 1;
			}

			// ͬ��
			if (handle->sync_only)
			{
				switch (type)
				{
				case 0xBA:
					// PS header
					ret_len = handle_header(handle, ps_buf + i, len - i);
					break;
					
				case 0xB9:
					// PS finish
					ret_len = handle_finish(handle, ps_buf + i, len - i);
					break;
					
				default:
					// PES
					ret_len = handle_common_pack(ps_buf + i, len - i);
					break;
				}
			}
			// �⸴�ã������ϸ��ý����Ϣ
			else
			{
				switch (type)
				{
				case 0xBA:
					// PS header
					ret_len = handle_header(handle, ps_buf + i, len - i);
					break;
					
				case 0xBB:
					// PS system header
					ret_len = handle_system_header(handle, ps_buf + i, len - i);
					break;
					
				case 0xBC:
					// PS map
					ret_len = handle_map(handle, ps_buf + i, len - i);
					break;
					
				case 0xB9:
					// PS finish
					ret_len = handle_finish(handle, ps_buf + i, len - i);
					break;
					
				default:
					// PES
					ret_len = handle_pes(handle, ps_buf + i, len - i);
					break;
				}
			}

			// ���һ���ͷ���
			break;
		}
	}

	if (!handle->ps_started) {
		return -1;
	}

	// ����������볤�ȣ�����0���´��ٽ���
	if (i + ret_len > len)
	{
		return 0;
	}

	handle->pack_ptr = ps_buf + i;
	handle->pack_len = ret_len;
	return i + ret_len;
}
