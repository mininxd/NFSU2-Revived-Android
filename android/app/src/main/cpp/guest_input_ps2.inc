// PlayStation 2 DirectInput objects. Same offsets, types and button bits as
// padObjectsXbox; only the names the controls screen shows change.
const PadObject padObjectsPs2[]={
    {0xa36d02e0u,0,0x0002u|(0u<<8),0x100u,"Left Stick X"},{0xa36d02e1u,4,0x0002u|(1u<<8),0x100u,"Left Stick Y"},
    {0xa36d02e2u,8,0x0002u|(2u<<8),0x100u,"L2 / R2"},{0xa36d02f4u,12,0x0002u|(3u<<8),0x100u,"Right Stick X"},
    {0xa36d02f5u,16,0x0002u|(4u<<8),0x100u,"Right Stick Y"},{0xa36d02e3u,20,0x0002u|(5u<<8),0x100u,"Right Stick Z"},
    {0xa36d02f2u,32,0x0010u|(0u<<8),0,"D-Pad"},
    {0xa36d02f0u,48,0x0004u|(0u<<8),0,"Cross"},{0xa36d02f0u,49,0x0004u|(1u<<8),0,"Circle"},
    {0xa36d02f0u,50,0x0004u|(2u<<8),0,"Square"},{0xa36d02f0u,51,0x0004u|(3u<<8),0,"Triangle"},
    {0xa36d02f0u,52,0x0004u|(4u<<8),0,"L1"},{0xa36d02f0u,53,0x0004u|(5u<<8),0,"R1"},
    {0xa36d02f0u,54,0x0004u|(6u<<8),0,"Select"},{0xa36d02f0u,55,0x0004u|(7u<<8),0,"Start"},
    {0xa36d02f0u,56,0x0004u|(8u<<8),0,"L3"},{0xa36d02f0u,57,0x0004u|(9u<<8),0,"R3"},
    {0xa36d02f0u,58,0x0004u|(10u<<8),0,"L2"},{0xa36d02f0u,59,0x0004u|(11u<<8),0,"R2"},
    {0xa36d02f0u,60,0x0004u|(12u<<8),0,"Button 12"},{0xa36d02f0u,61,0x0004u|(13u<<8),0,"Button 13"},
    {0xa36d02f0u,62,0x0004u|(14u<<8),0,"Button 14"},{0xa36d02f0u,63,0x0004u|(15u<<8),0,"Button 15"},
};
const char* padDeviceNamePs2="PlayStation 2 Controller";
static_assert(sizeof(padObjectsPs2)==sizeof(padObjectsXbox),"PS2 pad table must match Xbox object count");
