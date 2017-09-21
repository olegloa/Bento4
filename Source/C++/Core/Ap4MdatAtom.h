/*****************************************************************
|
|    AP4 - mdat Atoms
|
 ****************************************************************/

#ifndef _AP4_MDAT_ATOM_H_
#define _AP4_MDAT_ATOM_H_

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "Ap4Types.h"
#include "Ap4Atom.h"
#include "Ap4ContainerAtom.h"
#include "Ap4MoovAtom.h"
#include "Ap4Array.h"

/*----------------------------------------------------------------------
|   class references
+---------------------------------------------------------------------*/
class AP4_ByteStream;

/*----------------------------------------------------------------------
|   AP4_MdatAtom
+---------------------------------------------------------------------*/
class AP4_MdatAtom : public AP4_Atom
{
public:
    // constructor and destructor
	AP4_MdatAtom(AP4_UI64 size,
			     AP4_ByteStream&  stream);

	AP4_MdatAtom(const AP4_MdatAtom& other);
    ~AP4_MdatAtom();

    // methods
	AP4_Result Inspect(AP4_AtomInspector& inspector,
			           AP4_MoovAtom *moov,
					   AP4_ContainerAtom *moof,
					   AP4_Position base_data_offset);

    virtual AP4_Result Inspect(AP4_AtomInspector& inspector) { return AP4_Atom::Inspect(inspector); }

    virtual AP4_Result WriteFields(AP4_ByteStream& stream);
    virtual AP4_Atom*  Clone();
    AP4_Position       GetPosition() const { return m_SourcePosition; }

private:
    // members
    AP4_ByteStream* m_SourceStream;
    AP4_Position    m_SourcePosition;
};

#endif // _AP4_MDAT_ATOM_H_
