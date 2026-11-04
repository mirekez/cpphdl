#pragma once

// A test build selects this header only; dependencies remain library-only.
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <stdint.h>
namespace cpphdl_examples::math {
// Static methods are shared by ordinary RTL and the HLS policy adapters.
struct Fp32ExpMethods {
    static uint32_t exp2_table(uint32_t index) {
        if (index < 128u) {
            if (index < 64u) {
                if (index < 32u) {
                    if (index < 16u) {
                        if (index < 8u) {
                            if (index < 4u) {
                                if (index < 2u) {
                                    if (index < 1u) {
                                        return 1073741824u;
                                    } else {
                                        return 1076653033u;
                                    }
                                } else {
                                    if (index < 3u) {
                                        return 1079572136u;
                                    } else {
                                        return 1082499153u;
                                    }
                                }
                            } else {
                                if (index < 6u) {
                                    if (index < 5u) {
                                        return 1085434106u;
                                    } else {
                                        return 1088377016u;
                                    }
                                } else {
                                    if (index < 7u) {
                                        return 1091327906u;
                                    } else {
                                        return 1094286796u;
                                    }
                                }
                            }
                        } else {
                            if (index < 12u) {
                                if (index < 10u) {
                                    if (index < 9u) {
                                        return 1097253708u;
                                    } else {
                                        return 1100228665u;
                                    }
                                } else {
                                    if (index < 11u) {
                                        return 1103211687u;
                                    } else {
                                        return 1106202798u;
                                    }
                                }
                            } else {
                                if (index < 14u) {
                                    if (index < 13u) {
                                        return 1109202018u;
                                    } else {
                                        return 1112209370u;
                                    }
                                } else {
                                    if (index < 15u) {
                                        return 1115224875u;
                                    } else {
                                        return 1118248556u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 24u) {
                            if (index < 20u) {
                                if (index < 18u) {
                                    if (index < 17u) {
                                        return 1121280436u;
                                    } else {
                                        return 1124320536u;
                                    }
                                } else {
                                    if (index < 19u) {
                                        return 1127368878u;
                                    } else {
                                        return 1130425485u;
                                    }
                                }
                            } else {
                                if (index < 22u) {
                                    if (index < 21u) {
                                        return 1133490379u;
                                    } else {
                                        return 1136563583u;
                                    }
                                } else {
                                    if (index < 23u) {
                                        return 1139645120u;
                                    } else {
                                        return 1142735011u;
                                    }
                                }
                            }
                        } else {
                            if (index < 28u) {
                                if (index < 26u) {
                                    if (index < 25u) {
                                        return 1145833280u;
                                    } else {
                                        return 1148939949u;
                                    }
                                } else {
                                    if (index < 27u) {
                                        return 1152055042u;
                                    } else {
                                        return 1155178580u;
                                    }
                                }
                            } else {
                                if (index < 30u) {
                                    if (index < 29u) {
                                        return 1158310587u;
                                    } else {
                                        return 1161451085u;
                                    }
                                } else {
                                    if (index < 31u) {
                                        return 1164600099u;
                                    } else {
                                        return 1167757650u;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (index < 48u) {
                        if (index < 40u) {
                            if (index < 36u) {
                                if (index < 34u) {
                                    if (index < 33u) {
                                        return 1170923762u;
                                    } else {
                                        return 1174098458u;
                                    }
                                } else {
                                    if (index < 35u) {
                                        return 1177281762u;
                                    } else {
                                        return 1180473697u;
                                    }
                                }
                            } else {
                                if (index < 38u) {
                                    if (index < 37u) {
                                        return 1183674286u;
                                    } else {
                                        return 1186883552u;
                                    }
                                } else {
                                    if (index < 39u) {
                                        return 1190101520u;
                                    } else {
                                        return 1193328213u;
                                    }
                                }
                            }
                        } else {
                            if (index < 44u) {
                                if (index < 42u) {
                                    if (index < 41u) {
                                        return 1196563654u;
                                    } else {
                                        return 1199807867u;
                                    }
                                } else {
                                    if (index < 43u) {
                                        return 1203060876u;
                                    } else {
                                        return 1206322705u;
                                    }
                                }
                            } else {
                                if (index < 46u) {
                                    if (index < 45u) {
                                        return 1209593378u;
                                    } else {
                                        return 1212872918u;
                                    }
                                } else {
                                    if (index < 47u) {
                                        return 1216161350u;
                                    } else {
                                        return 1219458698u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 56u) {
                            if (index < 52u) {
                                if (index < 50u) {
                                    if (index < 49u) {
                                        return 1222764986u;
                                    } else {
                                        return 1226080238u;
                                    }
                                } else {
                                    if (index < 51u) {
                                        return 1229404479u;
                                    } else {
                                        return 1232737732u;
                                    }
                                }
                            } else {
                                if (index < 54u) {
                                    if (index < 53u) {
                                        return 1236080024u;
                                    } else {
                                        return 1239431376u;
                                    }
                                } else {
                                    if (index < 55u) {
                                        return 1242791816u;
                                    } else {
                                        return 1246161366u;
                                    }
                                }
                            }
                        } else {
                            if (index < 60u) {
                                if (index < 58u) {
                                    if (index < 57u) {
                                        return 1249540052u;
                                    } else {
                                        return 1252927899u;
                                    }
                                } else {
                                    if (index < 59u) {
                                        return 1256324931u;
                                    } else {
                                        return 1259731174u;
                                    }
                                }
                            } else {
                                if (index < 62u) {
                                    if (index < 61u) {
                                        return 1263146652u;
                                    } else {
                                        return 1266571390u;
                                    }
                                } else {
                                    if (index < 63u) {
                                        return 1270005413u;
                                    } else {
                                        return 1273448747u;
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                if (index < 96u) {
                    if (index < 80u) {
                        if (index < 72u) {
                            if (index < 68u) {
                                if (index < 66u) {
                                    if (index < 65u) {
                                        return 1276901417u;
                                    } else {
                                        return 1280363448u;
                                    }
                                } else {
                                    if (index < 67u) {
                                        return 1283834865u;
                                    } else {
                                        return 1287315695u;
                                    }
                                }
                            } else {
                                if (index < 70u) {
                                    if (index < 69u) {
                                        return 1290805962u;
                                    } else {
                                        return 1294305692u;
                                    }
                                } else {
                                    if (index < 71u) {
                                        return 1297814910u;
                                    } else {
                                        return 1301333643u;
                                    }
                                }
                            }
                        } else {
                            if (index < 76u) {
                                if (index < 74u) {
                                    if (index < 73u) {
                                        return 1304861917u;
                                    } else {
                                        return 1308399756u;
                                    }
                                } else {
                                    if (index < 75u) {
                                        return 1311947188u;
                                    } else {
                                        return 1315504238u;
                                    }
                                }
                            } else {
                                if (index < 78u) {
                                    if (index < 77u) {
                                        return 1319070932u;
                                    } else {
                                        return 1322647296u;
                                    }
                                } else {
                                    if (index < 79u) {
                                        return 1326233356u;
                                    } else {
                                        return 1329829140u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 88u) {
                            if (index < 84u) {
                                if (index < 82u) {
                                    if (index < 81u) {
                                        return 1333434672u;
                                    } else {
                                        return 1337049980u;
                                    }
                                } else {
                                    if (index < 83u) {
                                        return 1340675091u;
                                    } else {
                                        return 1344310030u;
                                    }
                                }
                            } else {
                                if (index < 86u) {
                                    if (index < 85u) {
                                        return 1347954824u;
                                    } else {
                                        return 1351609500u;
                                    }
                                } else {
                                    if (index < 87u) {
                                        return 1355274085u;
                                    } else {
                                        return 1358948606u;
                                    }
                                }
                            }
                        } else {
                            if (index < 92u) {
                                if (index < 90u) {
                                    if (index < 89u) {
                                        return 1362633090u;
                                    } else {
                                        return 1366327563u;
                                    }
                                } else {
                                    if (index < 91u) {
                                        return 1370032052u;
                                    } else {
                                        return 1373746586u;
                                    }
                                }
                            } else {
                                if (index < 94u) {
                                    if (index < 93u) {
                                        return 1377471191u;
                                    } else {
                                        return 1381205894u;
                                    }
                                } else {
                                    if (index < 95u) {
                                        return 1384950723u;
                                    } else {
                                        return 1388705706u;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (index < 112u) {
                        if (index < 104u) {
                            if (index < 100u) {
                                if (index < 98u) {
                                    if (index < 97u) {
                                        return 1392470869u;
                                    } else {
                                        return 1396246240u;
                                    }
                                } else {
                                    if (index < 99u) {
                                        return 1400031848u;
                                    } else {
                                        return 1403827719u;
                                    }
                                }
                            } else {
                                if (index < 102u) {
                                    if (index < 101u) {
                                        return 1407633882u;
                                    } else {
                                        return 1411450365u;
                                    }
                                } else {
                                    if (index < 103u) {
                                        return 1415277195u;
                                    } else {
                                        return 1419114401u;
                                    }
                                }
                            }
                        } else {
                            if (index < 108u) {
                                if (index < 106u) {
                                    if (index < 105u) {
                                        return 1422962010u;
                                    } else {
                                        return 1426820052u;
                                    }
                                } else {
                                    if (index < 107u) {
                                        return 1430688553u;
                                    } else {
                                        return 1434567544u;
                                    }
                                }
                            } else {
                                if (index < 110u) {
                                    if (index < 109u) {
                                        return 1438457051u;
                                    } else {
                                        return 1442357104u;
                                    }
                                } else {
                                    if (index < 111u) {
                                        return 1446267730u;
                                    } else {
                                        return 1450188960u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 120u) {
                            if (index < 116u) {
                                if (index < 114u) {
                                    if (index < 113u) {
                                        return 1454120821u;
                                    } else {
                                        return 1458063343u;
                                    }
                                } else {
                                    if (index < 115u) {
                                        return 1462016553u;
                                    } else {
                                        return 1465980482u;
                                    }
                                }
                            } else {
                                if (index < 118u) {
                                    if (index < 117u) {
                                        return 1469955159u;
                                    } else {
                                        return 1473940611u;
                                    }
                                } else {
                                    if (index < 119u) {
                                        return 1477936870u;
                                    } else {
                                        return 1481943963u;
                                    }
                                }
                            }
                        } else {
                            if (index < 124u) {
                                if (index < 122u) {
                                    if (index < 121u) {
                                        return 1485961921u;
                                    } else {
                                        return 1489990772u;
                                    }
                                } else {
                                    if (index < 123u) {
                                        return 1494030547u;
                                    } else {
                                        return 1498081275u;
                                    }
                                }
                            } else {
                                if (index < 126u) {
                                    if (index < 125u) {
                                        return 1502142985u;
                                    } else {
                                        return 1506215708u;
                                    }
                                } else {
                                    if (index < 127u) {
                                        return 1510299473u;
                                    } else {
                                        return 1514394310u;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            if (index < 192u) {
                if (index < 160u) {
                    if (index < 144u) {
                        if (index < 136u) {
                            if (index < 132u) {
                                if (index < 130u) {
                                    if (index < 129u) {
                                        return 1518500250u;
                                    } else {
                                        return 1522617322u;
                                    }
                                } else {
                                    if (index < 131u) {
                                        return 1526745556u;
                                    } else {
                                        return 1530884983u;
                                    }
                                }
                            } else {
                                if (index < 134u) {
                                    if (index < 133u) {
                                        return 1535035634u;
                                    } else {
                                        return 1539197537u;
                                    }
                                } else {
                                    if (index < 135u) {
                                        return 1543370725u;
                                    } else {
                                        return 1547555228u;
                                    }
                                }
                            }
                        } else {
                            if (index < 140u) {
                                if (index < 138u) {
                                    if (index < 137u) {
                                        return 1551751076u;
                                    } else {
                                        return 1555958300u;
                                    }
                                } else {
                                    if (index < 139u) {
                                        return 1560176931u;
                                    } else {
                                        return 1564406999u;
                                    }
                                }
                            } else {
                                if (index < 142u) {
                                    if (index < 141u) {
                                        return 1568648537u;
                                    } else {
                                        return 1572901575u;
                                    }
                                } else {
                                    if (index < 143u) {
                                        return 1577166143u;
                                    } else {
                                        return 1581442275u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 152u) {
                            if (index < 148u) {
                                if (index < 146u) {
                                    if (index < 145u) {
                                        return 1585730000u;
                                    } else {
                                        return 1590029350u;
                                    }
                                } else {
                                    if (index < 147u) {
                                        return 1594340357u;
                                    } else {
                                        return 1598663052u;
                                    }
                                }
                            } else {
                                if (index < 150u) {
                                    if (index < 149u) {
                                        return 1602997467u;
                                    } else {
                                        return 1607343634u;
                                    }
                                } else {
                                    if (index < 151u) {
                                        return 1611701585u;
                                    } else {
                                        return 1616071351u;
                                    }
                                }
                            }
                        } else {
                            if (index < 156u) {
                                if (index < 154u) {
                                    if (index < 153u) {
                                        return 1620452965u;
                                    } else {
                                        return 1624846459u;
                                    }
                                } else {
                                    if (index < 155u) {
                                        return 1629251865u;
                                    } else {
                                        return 1633669214u;
                                    }
                                }
                            } else {
                                if (index < 158u) {
                                    if (index < 157u) {
                                        return 1638098541u;
                                    } else {
                                        return 1642539877u;
                                    }
                                } else {
                                    if (index < 159u) {
                                        return 1646993254u;
                                    } else {
                                        return 1651458706u;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (index < 176u) {
                        if (index < 168u) {
                            if (index < 164u) {
                                if (index < 162u) {
                                    if (index < 161u) {
                                        return 1655936265u;
                                    } else {
                                        return 1660425963u;
                                    }
                                } else {
                                    if (index < 163u) {
                                        return 1664927835u;
                                    } else {
                                        return 1669441912u;
                                    }
                                }
                            } else {
                                if (index < 166u) {
                                    if (index < 165u) {
                                        return 1673968228u;
                                    } else {
                                        return 1678506817u;
                                    }
                                } else {
                                    if (index < 167u) {
                                        return 1683057710u;
                                    } else {
                                        return 1687620943u;
                                    }
                                }
                            }
                        } else {
                            if (index < 172u) {
                                if (index < 170u) {
                                    if (index < 169u) {
                                        return 1692196547u;
                                    } else {
                                        return 1696784557u;
                                    }
                                } else {
                                    if (index < 171u) {
                                        return 1701385007u;
                                    } else {
                                        return 1705997930u;
                                    }
                                }
                            } else {
                                if (index < 174u) {
                                    if (index < 173u) {
                                        return 1710623359u;
                                    } else {
                                        return 1715261330u;
                                    }
                                } else {
                                    if (index < 175u) {
                                        return 1719911875u;
                                    } else {
                                        return 1724575029u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 184u) {
                            if (index < 180u) {
                                if (index < 178u) {
                                    if (index < 177u) {
                                        return 1729250827u;
                                    } else {
                                        return 1733939301u;
                                    }
                                } else {
                                    if (index < 179u) {
                                        return 1738640488u;
                                    } else {
                                        return 1743354420u;
                                    }
                                }
                            } else {
                                if (index < 182u) {
                                    if (index < 181u) {
                                        return 1748081133u;
                                    } else {
                                        return 1752820662u;
                                    }
                                } else {
                                    if (index < 183u) {
                                        return 1757573041u;
                                    } else {
                                        return 1762338305u;
                                    }
                                }
                            }
                        } else {
                            if (index < 188u) {
                                if (index < 186u) {
                                    if (index < 185u) {
                                        return 1767116489u;
                                    } else {
                                        return 1771907628u;
                                    }
                                } else {
                                    if (index < 187u) {
                                        return 1776711757u;
                                    } else {
                                        return 1781528911u;
                                    }
                                }
                            } else {
                                if (index < 190u) {
                                    if (index < 189u) {
                                        return 1786359126u;
                                    } else {
                                        return 1791202437u;
                                    }
                                } else {
                                    if (index < 191u) {
                                        return 1796058879u;
                                    } else {
                                        return 1800928489u;
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                if (index < 224u) {
                    if (index < 208u) {
                        if (index < 200u) {
                            if (index < 196u) {
                                if (index < 194u) {
                                    if (index < 193u) {
                                        return 1805811301u;
                                    } else {
                                        return 1810707353u;
                                    }
                                } else {
                                    if (index < 195u) {
                                        return 1815616678u;
                                    } else {
                                        return 1820539314u;
                                    }
                                }
                            } else {
                                if (index < 198u) {
                                    if (index < 197u) {
                                        return 1825475297u;
                                    } else {
                                        return 1830424663u;
                                    }
                                } else {
                                    if (index < 199u) {
                                        return 1835387448u;
                                    } else {
                                        return 1840363688u;
                                    }
                                }
                            }
                        } else {
                            if (index < 204u) {
                                if (index < 202u) {
                                    if (index < 201u) {
                                        return 1845353420u;
                                    } else {
                                        return 1850356681u;
                                    }
                                } else {
                                    if (index < 203u) {
                                        return 1855373507u;
                                    } else {
                                        return 1860403934u;
                                    }
                                }
                            } else {
                                if (index < 206u) {
                                    if (index < 205u) {
                                        return 1865448001u;
                                    } else {
                                        return 1870505744u;
                                    }
                                } else {
                                    if (index < 207u) {
                                        return 1875577199u;
                                    } else {
                                        return 1880662405u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 216u) {
                            if (index < 212u) {
                                if (index < 210u) {
                                    if (index < 209u) {
                                        return 1885761398u;
                                    } else {
                                        return 1890874216u;
                                    }
                                } else {
                                    if (index < 211u) {
                                        return 1896000896u;
                                    } else {
                                        return 1901141476u;
                                    }
                                }
                            } else {
                                if (index < 214u) {
                                    if (index < 213u) {
                                        return 1906295993u;
                                    } else {
                                        return 1911464486u;
                                    }
                                } else {
                                    if (index < 215u) {
                                        return 1916646992u;
                                    } else {
                                        return 1921843549u;
                                    }
                                }
                            }
                        } else {
                            if (index < 220u) {
                                if (index < 218u) {
                                    if (index < 217u) {
                                        return 1927054196u;
                                    } else {
                                        return 1932278970u;
                                    }
                                } else {
                                    if (index < 219u) {
                                        return 1937517909u;
                                    } else {
                                        return 1942771053u;
                                    }
                                }
                            } else {
                                if (index < 222u) {
                                    if (index < 221u) {
                                        return 1948038440u;
                                    } else {
                                        return 1953320108u;
                                    }
                                } else {
                                    if (index < 223u) {
                                        return 1958616096u;
                                    } else {
                                        return 1963926443u;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (index < 240u) {
                        if (index < 232u) {
                            if (index < 228u) {
                                if (index < 226u) {
                                    if (index < 225u) {
                                        return 1969251188u;
                                    } else {
                                        return 1974590370u;
                                    }
                                } else {
                                    if (index < 227u) {
                                        return 1979944027u;
                                    } else {
                                        return 1985312200u;
                                    }
                                }
                            } else {
                                if (index < 230u) {
                                    if (index < 229u) {
                                        return 1990694927u;
                                    } else {
                                        return 1996092249u;
                                    }
                                } else {
                                    if (index < 231u) {
                                        return 2001504204u;
                                    } else {
                                        return 2006930832u;
                                    }
                                }
                            }
                        } else {
                            if (index < 236u) {
                                if (index < 234u) {
                                    if (index < 233u) {
                                        return 2012372174u;
                                    } else {
                                        return 2017828268u;
                                    }
                                } else {
                                    if (index < 235u) {
                                        return 2023299156u;
                                    } else {
                                        return 2028784876u;
                                    }
                                }
                            } else {
                                if (index < 238u) {
                                    if (index < 237u) {
                                        return 2034285470u;
                                    } else {
                                        return 2039800978u;
                                    }
                                } else {
                                    if (index < 239u) {
                                        return 2045331439u;
                                    } else {
                                        return 2050876895u;
                                    }
                                }
                            }
                        }
                    } else {
                        if (index < 248u) {
                            if (index < 244u) {
                                if (index < 242u) {
                                    if (index < 241u) {
                                        return 2056437387u;
                                    } else {
                                        return 2062012954u;
                                    }
                                } else {
                                    if (index < 243u) {
                                        return 2067603638u;
                                    } else {
                                        return 2073209480u;
                                    }
                                }
                            } else {
                                if (index < 246u) {
                                    if (index < 245u) {
                                        return 2078830522u;
                                    } else {
                                        return 2084466803u;
                                    }
                                } else {
                                    if (index < 247u) {
                                        return 2090118366u;
                                    } else {
                                        return 2095785251u;
                                    }
                                }
                            }
                        } else {
                            if (index < 252u) {
                                if (index < 250u) {
                                    if (index < 249u) {
                                        return 2101467502u;
                                    } else {
                                        return 2107165158u;
                                    }
                                } else {
                                    if (index < 251u) {
                                        return 2112878262u;
                                    } else {
                                        return 2118606857u;
                                    }
                                }
                            } else {
                                if (index < 254u) {
                                    if (index < 253u) {
                                        return 2124350982u;
                                    } else {
                                        return 2130110682u;
                                    }
                                } else {
                                    if (index < 255u) {
                                        return 2135885998u;
                                    } else {
                                        return 2141676973u;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // FP32 exponential approximation: Q8.24 range reduction, 256-entry exp2 table and
    // quadratic interpolation. FP32 interface, canonical NaN and FTZ outputs.
    // The approximation is independently checked against double-precision exp.
    static uint32_t fp32_exp_table(uint32_t a) {
        uint32_t magnitude, exponent, mantissa, shift, fixed, logarithm;
        uint32_t fraction, residual, t, polynomial, significand, retained, tail;
        int64_t y;
        int32_t power, output_exponent;
        bool negative;
        magnitude = a & 0x7fffffffu;
        exponent = magnitude >> 23;
        negative = (a >> 31) != 0;
        if (magnitude > 0x7f800000u)
            return 0x7fc00000u;
        if (magnitude > 0x42b40000u)
            return negative ? 0u : 0x7f800000u;
        if (exponent == 0)
            return 0x3f800000u;
        mantissa = (magnitude & 0x7fffffu) | 0x800000u;
        shift = (exponent >= 126 ? exponent - 126 : 126 - exponent);
        fixed = exponent >= 126 ? mantissa << (shift & 31)
                : shift >= 32   ? 0u
                                : mantissa >> (shift & 31);
        logarithm = uint32_t((uint64_t(fixed) * 1549082005ull + (1ull << 29)) >> 30);
        y = negative ? -int64_t(logarithm) : int64_t(logarithm);
        power = int32_t(y >> 24);
        fraction = uint32_t(y) & 0xffffffu;
        residual = fraction & 65535u;
        t = uint32_t((uint64_t(residual) * 744261118ull) >> 24);
        polynomial = 0x40000000u + t + uint32_t((uint64_t(t) * t) >> 31);
        significand =
            uint32_t((uint64_t(exp2_table(fraction >> 16)) * polynomial + (1ull << 29)) >> 30);
        retained = significand >> 7;
        tail = significand & 127u;
        if (tail > 64 || (tail == 64 && (retained & 1)))
            ++retained;
        if (retained >= 0x1000000u) {
            retained >>= 1;
            ++power;
        }
        output_exponent = 127 + power;
        if (output_exponent <= 0)
            return 0;
        if (output_exponent >= 255)
            return 0x7f800000u;
        return (uint32_t(output_exponent) << 23) | (retained & 0x7fffffu);
    }
};

inline uint32_t exp2_table(uint32_t index) {
    return Fp32ExpMethods::exp2_table(index);
}
inline uint32_t fp32_exp_table(uint32_t a) {
    return Fp32ExpMethods::fp32_exp_table(a);
}
} // namespace cpphdl_examples::math
#pragma pop_macro("MAINFILE")

#if defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include <cpphdl.h>

class Fp32ExpExample : public cpphdl::Module {
  public:
    _PORT(uint32_t) data_in;
    _PORT(uint32_t) result_out;
    void _assign() { result_out = _ASSIGN_COMB(result_comb_func()); }

  private:
    uint32_t result_comb = 0;
    uint32_t &result_comb_func() {
        result_comb = cpphdl_examples::math::Fp32ExpMethods::fp32_exp_table(data_in());
        return result_comb;
    }
};
#pragma pop_macro("MAINFILE")
#endif

#if !defined(SYNTHESIS) && defined(MAINFILE)
#pragma push_macro("MAINFILE")
#undef MAINFILE
#include "../../lib/math/Fp32Reference.h"
#ifdef VERILATOR
#include "VFp32ExpExample.h"
#endif
long _system_clock = 0;
int main_from_header(int, char**) {
    uint32_t data = 0;
#ifdef VERILATOR
    VFp32ExpExample dut;
#else
    Fp32ExpExample dut;
    dut.data_in = _ASSIGN(data);
    dut._assign();
#endif
    auto check = [&]() {
#ifdef VERILATOR
        dut.data_in = data;
        dut.eval();
        uint32_t result = dut.result_out;
#else
        uint32_t result = dut.result_out();
#endif
        // Check conversion exactly, and the approximation against host exp.
        fp_reference::check("exp RTL", result, cpphdl_examples::math::fp32_exp_table(data));
        uint32_t expected =
            fp_reference::flush(fp_reference::bits(std::exp(fp_reference::value(data))));
        if ((expected & 0x7f800000u) == 0x7f800000u || expected == 0)
            fp_reference::check("exp special/overflow/underflow", result, expected);
        else if (std::fabs(fp_reference::value(result) - fp_reference::value(expected)) >
                 2e-5f * fp_reference::value(expected))
            throw std::runtime_error("exp relative error exceeds 2e-5");
        ++_system_clock;
    };
    for (auto raw : fp_reference::edges) {
        data = raw;
        check();
    }
    for (int i = -100000; i <= 90000; i += 7) {
        data = fp_reference::bits(float(i) / 1000);
        check();
    }
    uint32_t state = 213;
    for (unsigned i = 0; i < 10000; ++i) {
        data = fp_reference::random(state);
        check();
    }
    std::puts("PASS FP32 exp: special values, range sweep, random inputs and 2e-5 relative error");
    return 0;
}
#pragma pop_macro("MAINFILE")
#endif
