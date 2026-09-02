#include <patcher.h>

__attribute__((weak))
__attribute__((at(0x00800910, "flashpatch")))
unsigned int flash_patch_0[2] = {0xb4f4f404, 0x46054607};

__attribute__((weak))
__attribute__((at(0x00805a40, "flashpatch")))
unsigned int flash_patch_1[2] = {0x9d8ef7ff, 0x49334616};

__attribute__((weak))
__attribute__((at(0x00805b98, "flashpatch")))
unsigned int flash_patch_2[2] = {0x008915bc, 0x9d73f7ff};

__attribute__((weak))
__attribute__((at(0x00805bb8, "flashpatch")))
unsigned int flash_patch_3[2] = {0x9d4af7ff, 0x2b404003};

__attribute__((weak))
__attribute__((at(0x00807bd8, "flashpatch")))
unsigned int flash_patch_4[2] = {0xbf00bd10, 0x9c34f7fa};

__attribute__((weak))
__attribute__((at(0x0080aba8, "flashpatch")))
unsigned int flash_patch_5[2] = {0x98d6f7fc, 0xb08b4606};

__attribute__((weak))
__attribute__((at(0x0080b978, "flashpatch")))
unsigned int flash_patch_6[2] = {0x9acff7fb, 0x46066805};

__attribute__((weak))
__attribute__((at(0x0080c980, "flashpatch")))
unsigned int flash_patch_7[2] = {0x47706840, 0x9ce6f7fa};

__attribute__((weak))
__attribute__((at(0x0080ca58, "flashpatch")))
unsigned int flash_patch_8[2] = {0xbf004770, 0x9cbbf7fa};

__attribute__((weak))
__attribute__((at(0x0080d4b8, "flashpatch")))
unsigned int flash_patch_9[2] = {0xbdf0b007, 0x9874f7fa};

__attribute__((weak))
__attribute__((at(0x0080d588, "flashpatch")))
unsigned int flash_patch_10[2] = {0x98b3f7fa, 0xb9355018};

__attribute__((weak))
__attribute__((at(0x0080d6d0, "flashpatch")))
unsigned int flash_patch_11[2] = {0xbdf0b03f, 0x9b3cf7f4};

__attribute__((weak))
__attribute__((at(0x0080d8b8, "flashpatch")))
unsigned int flash_patch_12[2] = {0xe7e10516, 0x9f6cf7f9};

__attribute__((weak))
__attribute__((at(0x0080f878, "flashpatch")))
unsigned int flash_patch_13[2] = {0x981bf7f8, 0x88114689};

__attribute__((weak))
__attribute__((at(0x008103d0, "flashpatch")))
unsigned int flash_patch_14[2] = {0xbd70b008, 0x9b96f7f7};

__attribute__((weak))
__attribute__((at(0x0081e9f0, "flashpatch")))
unsigned int flash_patch_15[2] = {0xbf00b81d, 0x9e5af7e3};

__attribute__((weak))
__attribute__((at(0x0081ee08, "flashpatch")))
unsigned int flash_patch_16[2] = {0x985cf7e4, 0xf0714604};

__attribute__((weak))
__attribute__((at(0x0081ef90, "flashpatch")))
unsigned int flash_patch_17[2] = {0x0000090c, 0x9e9cf7e3};

__attribute__((weak))
__attribute__((at(0x0081f0f0, "flashpatch")))
unsigned int flash_patch_18[2] = {0x00000914, 0x9eaaf7e3};

__attribute__((weak))
__attribute__((at(0x0081f3c8, "flashpatch")))
unsigned int flash_patch_19[2] = {0x9de5f7e9, 0xffeef7ff};

__attribute__((weak))
__attribute__((at(0x0081f3e0, "flashpatch")))
unsigned int flash_patch_20[2] = {0x9ddbf7e9, 0xffe8f7ff};

__attribute__((weak))
__attribute__((at(0x008214c0, "flashpatch")))
unsigned int flash_patch_21[2] = {0xbf00bd1c, 0x9e21f7e7};

__attribute__((weak))
__attribute__((at(0x00821840, "flashpatch")))
unsigned int flash_patch_22[2] = {0x47700001, 0x9ce1f7e7};

__attribute__((weak))
__attribute__((at(0x00826f70, "flashpatch")))
unsigned int flash_patch_23[2] = {0x0088ca50, 0x9a3af7e2};

__attribute__((weak))
__attribute__((at(0x00827ae8, "flashpatch")))
unsigned int flash_patch_24[2] = {0xbf008ff0, 0x9e34f7e1};

__attribute__((weak))
__attribute__((at(0x00828610, "flashpatch")))
unsigned int flash_patch_25[2] = {0x9d1ff7e1, 0x7018f44f};

__attribute__((weak))
__attribute__((at(0x0082dbc0, "flashpatch")))
unsigned int flash_patch_26[2] = {0x9c01f7dc, 0x4605684b};

/*__attribute__((weak))
__attribute__((at(0x0082ea60, "flashpatch")))
unsigned int flash_patch_27[2] = {0x998df7dc, 0x68438997};*/

__attribute__((weak))
__attribute__((at(0x0082f4f0, "flashpatch")))
unsigned int flash_patch_28[2] = {0xbf00bcc5, 0x9ad0f7db};

__attribute__((weak))
__attribute__((at(0x008334f0, "flashpatch")))
unsigned int flash_patch_29[2] = {0xbf00bd1c, 0x9eb4f7d8};

__attribute__((weak))
__attribute__((at(0x00833930, "flashpatch")))
unsigned int flash_patch_30[2] = {0xbd08b280, 0x9a94f7d7};

__attribute__((weak))
__attribute__((at(0x00834918, "flashpatch")))
unsigned int flash_patch_31[2] = {0xbf00bdf8, 0x9e0ef7d6};

__attribute__((weak))
__attribute__((at(0x008351e8, "flashpatch")))
unsigned int flash_patch_32[2] = {0xbdf8602b, 0x9ddaf7d6};

__attribute__((weak))
__attribute__((at(0x00835380, "flashpatch")))
unsigned int flash_patch_33[2] = {0x98c4f7dc, 0x68054604};

__attribute__((weak))
__attribute__((at(0x008360e8, "flashpatch")))
unsigned int flash_patch_34[2] = {0x008957cc, 0x99b0f7d6};

__attribute__((weak))
__attribute__((at(0x00837e78, "flashpatch")))
unsigned int flash_patch_35[2] = {0xbf00bd10, 0x9df2f7d3};

__attribute__((weak))
__attribute__((at(0x00838be8, "flashpatch")))
unsigned int flash_patch_36[2] = {0x9ea4f7d2, 0x46054622};

__attribute__((weak))
__attribute__((at(0x0083a1d0, "flashpatch")))
unsigned int flash_patch_37[2] = {0x9e29f7d1, 0x4610460c};

__attribute__((weak))
__attribute__((at(0x0083a228, "flashpatch")))
unsigned int flash_patch_38[2] = {0xbd102000, 0x9ee4f7d1};

__attribute__((weak))
__attribute__((at(0x0083a4d0, "flashpatch")))
unsigned int flash_patch_39[2] = {0x9d20f7d1, 0x7f634605};

__attribute__((weak))
__attribute__((at(0x0083b598, "flashpatch")))
unsigned int flash_patch_40[2] = {0x008952a8, 0x9de1f7d0};

__attribute__((weak))
__attribute__((at(0x0083c730, "flashpatch")))
unsigned int flash_patch_41[2] = {0xbd38b801, 0x9f30f7d4};

__attribute__((weak))
__attribute__((at(0x0083d710, "flashpatch")))
unsigned int flash_patch_42[2] = {0x9ab4f7d4, 0xf892461e};

__attribute__((weak))
__attribute__((at(0x0083db88, "flashpatch")))
unsigned int flash_patch_43[2] = {0x0089642e, 0x9f74f7d3};

__attribute__((weak))
__attribute__((at(0x0083dde8, "flashpatch")))
unsigned int flash_patch_44[2] = {0x9d66f7d3, 0xa001f891};

__attribute__((weak))
__attribute__((at(0x0083dfd8, "flashpatch")))
unsigned int flash_patch_45[2] = {0x9f4af7d3, 0x46157853};

__attribute__((weak))
__attribute__((at(0x0083e228, "flashpatch")))
unsigned int flash_patch_46[2] = {0xbd1030ff, 0x99f8f7d3};

__attribute__((weak))
__attribute__((at(0x0083f708, "flashpatch")))
unsigned int flash_patch_47[2] = {0x9e6cf7d7, 0x8014f8d0};

__attribute__((weak))
__attribute__((at(0x00840210, "flashpatch")))
unsigned int flash_patch_48[2] = {0x9f2ef7d6, 0x920ab093};

__attribute__((weak))
__attribute__((at(0x008423a8, "flashpatch")))
unsigned int flash_patch_49[2] = {0x9c86f7d5, 0xf8934605};

__attribute__((weak))
__attribute__((at(0x00842438, "flashpatch")))
unsigned int flash_patch_50[2] = {0x9c47f7d5, 0x41f0e92d};

__attribute__((weak))
__attribute__((at(0x00842d08, "flashpatch")))
unsigned int flash_patch_51[2] = {0xbf00becd, 0x9873f7d5};

__attribute__((weak))
__attribute__((at(0x00845790, "flashpatch")))
unsigned int flash_patch_52[2] = {0x00897af0, 0x99d1f7d4};

__attribute__((weak))
__attribute__((at(0x00847350, "flashpatch")))
unsigned int flash_patch_53[2] = {0x00897ba0, 0x987af7d2};

__attribute__((weak))
__attribute__((at(0x00847928, "flashpatch")))
unsigned int flash_patch_54[2] = {0x9e2ff7d0, 0x402af9b1};

__attribute__((weak))
__attribute__((at(0x008489b8, "flashpatch")))
unsigned int flash_patch_55[2] = {0x9b67f7d1, 0x3320f8d0};

__attribute__((weak))
__attribute__((at(0x00849020, "flashpatch")))
unsigned int flash_patch_56[2] = {0xbf00bdf0, 0x9c18f7cf};

__attribute__((weak))
__attribute__((at(0x00849260, "flashpatch")))
unsigned int flash_patch_57[2] = {0x99f0f7d0, 0x47f0e92d};

__attribute__((weak))
__attribute__((at(0x0084af80, "flashpatch")))
unsigned int flash_patch_58[2] = {0x99eaf7d1, 0x4604b5f7};

__attribute__((weak))
__attribute__((at(0x0084b468, "flashpatch")))
unsigned int flash_patch_59[2] = {0x00889758, 0x9fd1f7cf};

__attribute__((weak))
__attribute__((at(0x0084b4b0, "flashpatch")))
unsigned int flash_patch_60[2] = {0xc0004080, 0x9f9ef7cf};

__attribute__((weak))
__attribute__((at(0x0084ce10, "flashpatch")))
unsigned int flash_patch_61[2] = {0xbf004770, 0x9ce6f7ce};

__attribute__((weak))
__attribute__((at(0x0084d118, "flashpatch")))
unsigned int flash_patch_62[2] = {0xbd102000, 0x9b0ef7ce};

__attribute__((weak))
__attribute__((at(0x0084dc30, "flashpatch")))
unsigned int flash_patch_63[2] = {0xbf00bd38, 0x9da0f7ce};

__attribute__((weak))
__attribute__((at(0x0084dfa0, "flashpatch")))
unsigned int flash_patch_64[2] = {0x9a3cf7ce, 0x46053134};

__attribute__((weak))
__attribute__((at(0x0084e638, "flashpatch")))
unsigned int flash_patch_65[2] = {0xbf00b9ab, 0x993cf7ce};

__attribute__((weak))
__attribute__((at(0x0084fe88, "flashpatch")))
unsigned int flash_patch_66[2] = {0x9f04f7cc, 0x4605460a};

__attribute__((weak))
__attribute__((at(0x00855428, "flashpatch")))
unsigned int flash_patch_67[2] = {0x9fb7f7c7, 0xb08d6845};

__attribute__((weak))
__attribute__((at(0x00855f48, "flashpatch")))
unsigned int flash_patch_68[2] = {0xbdb0f000, 0x9904f7c7};

__attribute__((weak))
__attribute__((at(0x00857950, "flashpatch")))
unsigned int flash_patch_69[2] = {0xbdfe2001, 0x9e80f7c5};

__attribute__((weak))
__attribute__((at(0x00857e38, "flashpatch")))
unsigned int flash_patch_70[2] = {0xbf0081fc, 0x9c37f7c5};

__attribute__((weak))
__attribute__((at(0x00859990, "flashpatch")))
unsigned int flash_patch_71[2] = {0x9efcf7c3, 0xf8ddb085};

__attribute__((weak))
__attribute__((at(0x00859c08, "flashpatch")))
unsigned int flash_patch_72[2] = {0x9db2f7c3, 0xf920f7a9};

__attribute__((weak))
__attribute__((at(0x0085d610, "flashpatch")))
unsigned int flash_patch_73[2] = {0x0088eaa8, 0x9b56f7c0};

__attribute__((weak))
__attribute__((at(0x0085e100, "flashpatch")))
unsigned int flash_patch_74[2] = {0x9e0ef7bf, 0x8012f8b0};

__attribute__((weak))
__attribute__((at(0x0085ff90, "flashpatch")))
unsigned int flash_patch_75[2] = {0xbf00bdfe, 0x9ffaf7bd};

__attribute__((weak))
__attribute__((at(0x00860568, "flashpatch")))
unsigned int flash_patch_76[2] = {0x9df8f7bd, 0x688cab05};

__attribute__((weak))
__attribute__((at(0x00860ff0, "flashpatch")))
unsigned int flash_patch_77[2] = {0xbf004770, 0x997ff7bd};

__attribute__((weak))
__attribute__((at(0x00863690, "flashpatch")))
unsigned int flash_patch_78[2] = {0x9f50f7ba, 0x681b4605};

__attribute__((weak))
__attribute__((at(0x00864ec8, "flashpatch")))
unsigned int flash_patch_79[2] = {0x9992f7ba, 0x6178f8d0};

__attribute__((weak))
__attribute__((at(0x008656e0, "flashpatch")))
unsigned int flash_patch_80[2] = {0xbf00bdf8, 0x9db1f7b9};

__attribute__((weak))
__attribute__((at(0x00865e80, "flashpatch")))
unsigned int flash_patch_81[2] = {0xbf0087f0, 0x9bdff7b9};

__attribute__((weak))
__attribute__((at(0x008667b0, "flashpatch")))
unsigned int flash_patch_82[2] = {0x9fe9f7b8, 0x460cb085};

__attribute__((weak))
__attribute__((at(0x00866a98, "flashpatch")))
unsigned int flash_patch_83[2] = {0xbf00bcd3, 0x9d99f7b8};

__attribute__((weak))
__attribute__((at(0x00869830, "flashpatch")))
unsigned int flash_patch_84[2] = {0x00891d77, 0x99b3f7b6};

__attribute__((weak))
__attribute__((at(0x0086c378, "flashpatch")))
unsigned int flash_patch_85[2] = {0xbf00bdf0, 0x9b68f7b3};

__attribute__((weak))
__attribute__((at(0x0086c620, "flashpatch")))
unsigned int flash_patch_86[2] = {0x9a78f7b3, 0x4607680d};

__attribute__((weak))
__attribute__((at(0x0086cfd8, "flashpatch")))
unsigned int flash_patch_87[2] = {0x0089bde5, 0x9e3ff7b2};

__attribute__((weak))
__attribute__((at(0x0086d088, "flashpatch")))
unsigned int flash_patch_88[2] = {0x9df5f7b2, 0x680b18c9};

__attribute__((weak))
__attribute__((at(0x0086f8e8, "flashpatch")))
unsigned int flash_patch_89[2] = {0xbf00bc75, 0x99b0f7ba};

__attribute__((weak))
__attribute__((at(0x00870d08, "flashpatch")))
unsigned int flash_patch_90[2] = {0x9c88f7b9, 0x46144b35};

__attribute__((weak))
__attribute__((at(0x00875b60, "flashpatch")))
unsigned int flash_patch_91[2] = {0x0089d85f, 0x9f76f7b8};

__attribute__((weak))
__attribute__((at(0x00877e18, "flashpatch")))
unsigned int flash_patch_92[2] = {0x87f0e8bd, 0x99a2f7b7};

__attribute__((weak))
__attribute__((at(0x00878b78, "flashpatch")))
unsigned int flash_patch_93[2] = {0x989af7b6, 0x920bb0db};

__attribute__((weak))
__attribute__((at(0x0087a130, "flashpatch")))
unsigned int flash_patch_94[2] = {0x9c9bf7b5, 0xf8bdb085};

__attribute__((weak))
__attribute__((at(0x0087c2f0, "flashpatch")))
unsigned int flash_patch_95[2] = {0x0089dbbe, 0x9ab4f7b4};

__attribute__((weak))
__attribute__((at(0x0087e510, "flashpatch")))
unsigned int flash_patch_96[2] = {0x0089dee4, 0x9cf7f7b2};

__attribute__((weak))
__attribute__((at(0x0087ee48, "flashpatch")))
unsigned int flash_patch_97[2] = {0xbf004770, 0x99cef7b2};

