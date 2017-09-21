/*****************************************************************
|
|    AP4 - mdat Atoms
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Ap4MdatAtom.h"
#include "Ap4AtomFactory.h"
#include "Ap4Utils.h"
#include "Ap4TfhdAtom.h"
#include "Ap4TrunAtom.h"
#include "Ap4TrexAtom.h"
#include "Ap4StsdAtom.h"
#include "Ap4TrakAtom.h"
#include "Ap4SampleEntry.h"
#include "Ap4AvccAtom.h"
#include "Ap4HvccAtom.h"
#include "Ap4BitStream.h"
#include "Ap4AvcParser.h"
#include "Ap4HevcParser.h"

/*----------------------------------------------------------------------
|   AP4_MdatAtom::AP4_MdatAtom
+---------------------------------------------------------------------*/
AP4_MdatAtom::AP4_MdatAtom(AP4_UI64        size,
                           AP4_ByteStream& stream) :
    AP4_Atom(AP4_ATOM_TYPE_MDAT, size),
    m_SourceStream(&stream)
{
    // store source stream position
    stream.Tell(m_SourcePosition);

    // clamp to the file size
    AP4_UI64 file_size;
    if (AP4_SUCCEEDED(stream.GetSize(file_size))) {
        if (m_SourcePosition-GetHeaderSize()+size > file_size) {
            if (m_Size32 == 1) {
                // size is encoded as a large size
                m_Size64 = file_size-m_SourcePosition;
            } else {
                AP4_ASSERT(size <= 0xFFFFFFFF);
                m_Size32 = (AP4_UI32)(file_size-m_SourcePosition);
            }
        }
    }

    // keep a reference to the source stream
    m_SourceStream->AddReference();
}

/*----------------------------------------------------------------------
|   AP4_MdatAtom::AP4_MdatAtom
+---------------------------------------------------------------------*/
AP4_MdatAtom::AP4_MdatAtom(const AP4_MdatAtom& other) :
    AP4_Atom(other.m_Type, (AP4_UI32)0),
    m_SourceStream(other.m_SourceStream),
    m_SourcePosition(other.m_SourcePosition)
{
    m_Size32 = other.m_Size32;
    m_Size64 = other.m_Size64;
    if (m_SourceStream) {
        m_SourceStream->AddReference();
    }
}

/*----------------------------------------------------------------------
|   AP4_MdatAtom::~AP4_MdatAtom
+---------------------------------------------------------------------*/
AP4_MdatAtom::~AP4_MdatAtom()
{
    // release the source stream reference
    if (m_SourceStream) {
        m_SourceStream->Release();
    }
}

/*----------------------------------------------------------------------
|   AP4_MdatAtom::WriteFields
+---------------------------------------------------------------------*/
AP4_Result
AP4_MdatAtom::WriteFields(AP4_ByteStream& stream)
{
    AP4_Result result;

    // remember the source position
    AP4_Position position;
    m_SourceStream->Tell(position);

    // seek into the source at the stored offset
    result = m_SourceStream->Seek(m_SourcePosition);
    if (AP4_FAILED(result)) return result;

    // copy the source stream to the output
    AP4_UI64 payload_size = GetSize()-GetHeaderSize();
    result = m_SourceStream->CopyTo(stream, payload_size);
    if (AP4_FAILED(result)) return result;

    // restore the original stream position
    m_SourceStream->Seek(position);

    return AP4_SUCCESS;
}

/*----------------------------------------------------------------------
|   AP4_MdatAtom::Clone
+---------------------------------------------------------------------*/
AP4_Atom*
AP4_MdatAtom::Clone()
{
    return new AP4_MdatAtom(*this);
}

static unsigned int ReadGolomb(AP4_BitStream& bits)
{
    unsigned int leading_zeros = 0;
    while (bits.ReadBit() == 0) {
        leading_zeros++;
    }
    if (leading_zeros) {
        return (1<<leading_zeros)-1+bits.ReadBits(leading_zeros);
    } else {
        return 0;
    }
}

static void GetAvcNalInfo(unsigned char *nalu_payload,
		                  char *nal_value,
						  size_t nal_value_size)
{
    unsigned int         nalu_type = nalu_payload[0] & 0x1F;
    const char*          nalu_type_name = AP4_AvcNalParser::NaluTypeName(nalu_type);
    if (nalu_type_name == NULL) nalu_type_name = "UNKNOWN";

    if (nalu_type == AP4_AVC_NAL_UNIT_TYPE_ACCESS_UNIT_DELIMITER) {
        unsigned int primary_pic_type = (nalu_payload[1]>>5);
        const char*  primary_pic_type_name = AP4_AvcNalParser::PrimaryPicTypeName(primary_pic_type);
        if (primary_pic_type_name == NULL) primary_pic_type_name = "UNKNOWN";

        AP4_FormatString(nal_value, nal_value_size, "[%d:%s]", primary_pic_type, primary_pic_type_name);
    } else if (nalu_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_NON_IDR_PICTURE ||
    		   nalu_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_DATA_PARTITION_A ||
			   nalu_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE ||
			   nalu_type == AP4_AVC_NAL_UNIT_TYPE_CODED_SLICE_OF_AUXILIARY_PICTURE) {
        AP4_BitStream bits;
        bits.WriteBytes(&nalu_payload[1], 8);
        ReadGolomb(bits);

        unsigned int slice_type = ReadGolomb(bits);
        const char* slice_type_name = AP4_AvcNalParser::SliceTypeName(slice_type);
        if (slice_type_name == NULL) slice_type_name = "?";

        AP4_FormatString(nal_value, nal_value_size, "slice=%d (%s)", slice_type, slice_type_name);
    } else {
    	nal_value[0] = 0;
    }
}

static void GetHevcNalInfo(unsigned char *nalu_payload,
		                  char *nal_value,
						  size_t nal_value_size)
{
    unsigned int         nalu_type = (nalu_payload[0] >> 1) & 0x3F;
    const char*          nalu_type_name = AP4_HevcNalParser::NaluTypeName(nalu_type);
    if (nalu_type_name == NULL) nalu_type_name = "UNKNOWN";

    if (nalu_type == AP4_HEVC_NALU_TYPE_AUD_NUT) {
        unsigned int primary_pic_type = (nalu_payload[1]>>5);
        const char*  primary_pic_type_name = AP4_HevcNalParser::PicTypeName(primary_pic_type);
        if (primary_pic_type_name == NULL) primary_pic_type_name = "UNKNOWN";

        AP4_FormatString(nal_value, nal_value_size, "[%d:%s]", primary_pic_type, primary_pic_type_name);
    } else {
    	nal_value[0] = 0;
    }
}

/*----------------------------------------------------------------------
|   AP4_MdatAtom::Inspect
+---------------------------------------------------------------------*/
AP4_Result
AP4_MdatAtom::Inspect(AP4_AtomInspector& inspector,
		              AP4_MoovAtom *moov,
					  AP4_ContainerAtom *moof,
					  AP4_Position base_data_offset)
{
    InspectHeader(inspector);

    if (inspector.GetVerbosity() > 3 && moov != NULL && moof != NULL) {
        AP4_Position position;
        m_SourceStream->Tell(position);

    	AP4_TfhdAtom* tfhd = AP4_DYNAMIC_CAST(AP4_TfhdAtom, moof->FindChild("traf/tfhd"));
    	AP4_TrunAtom* trun = AP4_DYNAMIC_CAST(AP4_TrunAtom, moof->FindChild("traf/trun"));
    	AP4_ContainerAtom* mvex = AP4_DYNAMIC_CAST(AP4_ContainerAtom, moov->FindChild("mvex"));
    	AP4_TrakAtom* trak = NULL;
    	AP4_TrexAtom* trex = NULL;
        unsigned int nalu_length_size = 0;

    	if (mvex && tfhd) {
			for (AP4_List<AP4_Atom>::Item* mvex_item = mvex->GetChildren().FirstItem(); mvex_item; mvex_item = mvex_item->GetNext()) {
                AP4_Atom* atom = mvex_item->GetData();
                if (atom->GetType() == AP4_ATOM_TYPE_TREX) {
                    trex = AP4_DYNAMIC_CAST(AP4_TrexAtom, atom);
                    if (trex && trex->GetTrackId() == tfhd->GetTrackId()) {
                    	break;
                    } else {
                    	trex = NULL;
                    }
                }
			}
    	}

    	AP4_AvccAtom* avcc = NULL;
    	AP4_HvccAtom* hvcc = NULL;

		for (AP4_List<AP4_TrakAtom>::Item* trak_item = moov->GetTrakAtoms().FirstItem(); trak_item; trak_item = trak_item->GetNext()) {
			trak = trak_item->GetData();
			if (trak && trak->GetId() == tfhd->GetTrackId()) {
				AP4_StsdAtom* stsd = AP4_DYNAMIC_CAST(AP4_StsdAtom, trak->FindChild("mdia/minf/stbl/stsd"));
				if (stsd) {
					for (AP4_List<AP4_Atom>::Item* stsd_item = stsd->GetChildren().FirstItem(); stsd_item; stsd_item = stsd_item->GetNext()) {
						AP4_VisualSampleEntry* vse = AP4_DYNAMIC_CAST(AP4_VisualSampleEntry, stsd_item->GetData());
						if (vse) {
							avcc = AP4_DYNAMIC_CAST(AP4_AvccAtom, vse->FindChild("avcC"));
							if (avcc) {
								nalu_length_size = avcc->GetNaluLengthSize();
							} else {
								hvcc = AP4_DYNAMIC_CAST(AP4_HvccAtom, vse->FindChild("hvcC"));
								if (hvcc) {
									nalu_length_size = hvcc->GetNaluLengthSize();
								}
							}
						}
					}
				}
				break;
			} else {
				trak = NULL;
			}
		}

    	if (tfhd && trun && trex && trak && nalu_length_size) {
            AP4_Flags tfhd_flags = tfhd->GetFlags();
            AP4_Flags trun_flags = trun->GetFlags();

            // base data offset
            AP4_Position data_offset = base_data_offset;
            if (tfhd_flags & AP4_TFHD_FLAG_BASE_DATA_OFFSET_PRESENT) {
                data_offset = tfhd->GetBaseDataOffset();
            }
            if (trun_flags & AP4_TRUN_FLAG_DATA_OFFSET_PRESENT) {
                data_offset += trun->GetDataOffset();
            }

            // default sample size
            AP4_UI32 default_sample_size = 0;
            if (tfhd_flags & AP4_TFHD_FLAG_DEFAULT_SAMPLE_SIZE_PRESENT) {
                default_sample_size = tfhd->GetDefaultSampleSize();
            } else if (trex) {
                default_sample_size = trex->GetDefaultSampleSize();
            }

            for (unsigned int sample_id = 0, nal_id = 0; sample_id<trun->GetEntries().ItemCount(); sample_id++) {
                AP4_UI32 sample_size;

                if (trun_flags & AP4_TRUN_FLAG_SAMPLE_SIZE_PRESENT) {
                	sample_size = trun->GetEntries()[sample_id].sample_size;
                } else {
                	sample_size = default_sample_size;
                }

                AP4_UI64 nal_pos = data_offset;
                AP4_UI64 nal_end = data_offset + sample_size;

                // process the sample data, one NALU at a time
                while (nal_end - nal_pos > nalu_length_size) {
                	m_SourceStream->Seek(nal_pos);
                    unsigned int nalu_length = 0;
                    unsigned char buffer[4];
                    switch (nalu_length_size) {
                        case 1:
                        	m_SourceStream->Read(buffer, 1);
                            nalu_length = buffer[0];
                            break;

                        case 2:
                        	m_SourceStream->Read(buffer, 2);
                            nalu_length = AP4_BytesToUInt16BE(buffer);
                            break;

                        case 4:
                        	m_SourceStream->Read(buffer, 4);
                            nalu_length = AP4_BytesToUInt32BE(buffer);
                            break;

                        default:
                            return AP4_ERROR_INVALID_FORMAT;
                    }

                    unsigned char *nalu_payload = new unsigned char[nalu_length];
                    m_SourceStream->Read(nalu_payload, nalu_length);

                    unsigned int         nalu_type = nalu_payload[0]&0x1F;
                    const char*          nalu_type_name = AP4_AvcNalParser::NaluTypeName(nalu_type);
                    if (nalu_type_name == NULL) nalu_type_name = "UNKNOWN";

                    char nal_value[256];
                    if (avcc != NULL) {
                        GetAvcNalInfo(nalu_payload, nal_value, sizeof(nal_value));
                    } else if (hvcc != NULL) {
                        GetHevcNalInfo(nalu_payload, nal_value, sizeof(nal_value));
                    } else {
                    	nal_value[0] = 0;
                    }
                    delete nalu_payload;

                    char nal_header[256];
                    AP4_FormatString(nal_header, sizeof(nal_header), "s:%02d:n:%04d:t:%02d (%s)", sample_id, nal_id, nalu_type, nalu_type_name);

                    inspector.AddField(nal_header, nal_value);

                    nal_pos += nalu_length + nalu_length_size;
                    if (nal_pos > nal_end) {
                        return AP4_ERROR_INVALID_FORMAT;
                    }
                    nal_id++;
                }

                // check for some bytes in sample outside nals
                if (nal_pos != nal_end) {
                    return AP4_ERROR_INVALID_FORMAT;
                }

                data_offset += sample_size;
            }
		}

    	// restore the previous stream position
    	m_SourceStream->Seek(position);
    } else {
    	InspectFields(inspector);
    }
    inspector.EndAtom();

    return AP4_SUCCESS;
}
