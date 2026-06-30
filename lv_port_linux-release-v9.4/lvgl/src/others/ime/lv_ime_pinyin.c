/**
 * @file lv_ime_pinyin.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_ime_pinyin_private.h"
#include "../../core/lv_obj_class_private.h"
#if LV_USE_IME_PINYIN != 0

#include "../../lvgl.h"
#include "../../core/lv_global.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_ime_pinyin_class)
#define cand_len LV_GLOBAL_DEFAULT()->ime_cand_len

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_ime_pinyin_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_ime_pinyin_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj);
static void lv_ime_pinyin_style_change_event(lv_event_t * e);
static void lv_ime_pinyin_kb_event(lv_event_t * e);
static void lv_ime_pinyin_cand_panel_event(lv_event_t * e);

static void init_pinyin_dict(lv_obj_t * obj, const lv_pinyin_dict_t * dict);
static void pinyin_input_proc(lv_obj_t * obj);
static void pinyin_page_proc(lv_obj_t * obj, uint16_t btn);
static char * pinyin_search_matching(lv_obj_t * obj, char * py_str, uint16_t * cand_num);
static void pinyin_ime_clear_data(lv_obj_t * obj);

#if LV_IME_PINYIN_USE_K9_MODE
    static void pinyin_k9_init_data(lv_obj_t * obj);
    static void pinyin_k9_get_legal_py(lv_obj_t * obj, char * k9_input, const char * py9_map[]);
    static bool pinyin_k9_is_valid_py(lv_obj_t * obj, char * py_str);
    static void pinyin_k9_fill_cand(lv_obj_t * obj);
    static void pinyin_k9_cand_page_proc(lv_obj_t * obj, uint16_t dir);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/

const lv_obj_class_t lv_ime_pinyin_class = {
    .constructor_cb = lv_ime_pinyin_constructor,
    .destructor_cb  = lv_ime_pinyin_destructor,
    .width_def      = LV_SIZE_CONTENT,
    .height_def     = LV_SIZE_CONTENT,
    .group_def      = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size  = sizeof(lv_ime_pinyin_t),
    .base_class     = &lv_obj_class,
    .name = "lv_ime_pinyin",
};

#if LV_IME_PINYIN_USE_K9_MODE
static const char * lv_btnm_def_pinyin_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 21] = {\
                                                                                      ",\0", "123\0",  "abc \0", "def\0",  LV_SYMBOL_BACKSPACE"\0", "\n\0",
                                                                                      ".\0", "ghi\0", "jkl\0", "mno\0",  LV_SYMBOL_KEYBOARD"\0", "\n\0",
                                                                                      "?\0", "pqrs\0", "tuv\0", "wxyz\0",  LV_SYMBOL_NEW_LINE"\0", "\n\0",
                                                                                      LV_SYMBOL_LEFT"\0", "\0"
                                                                                     };

static lv_buttonmatrix_ctrl_t default_kb_ctrl_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 17] = { 1 };
static char   lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 2][LV_IME_PINYIN_K9_MAX_INPUT] = {0};
#endif

static char   lv_pinyin_cand_str[LV_IME_PINYIN_CAND_TEXT_NUM][4];
static char * lv_btnm_def_pinyin_sel_map[LV_IME_PINYIN_CAND_TEXT_NUM + 3];

#if LV_IME_PINYIN_USE_DEFAULT_DICT
static const lv_pinyin_dict_t lv_ime_pinyin_def_dict[] = {
    { "a", "啊阿吖呵" },
    { "ai", "爱埃挨矮碍癌唉" },
    { "an", "安按岸暗案庵氨俺" },
    { "ang", "昂肮盎" },
    { "ao", "奥傲熬凹袄澳" },
    { "ba", "吧把爸八拔霸坝扒叭" },
    { "bai", "百白败摆拜佰" },
    { "ban", "半般办板版斑搬伴瓣" },
    { "bang", "帮棒榜傍邦绑镑" },
    { "bao", "保报包抱爆宝饱胞暴剥" },
    { "bei", "被背杯北备贝倍辈碑悲" },
    { "ben", "本奔笨苯" },
    { "beng", "崩蹦泵绷甭" },
    { "bi", "比必笔闭鼻币碧避彼鄙" },
    { "bian", "变边便遍编辨辩贬扁" },
    { "biao", "表标彪膘飙" },
    { "bie", "别憋鳖瘪" },
    { "bin", "宾滨斌彬濒" },
    { "bing", "病冰并兵柄丙饼秉" },
    { "bo", "波播博薄伯勃菠玻搏" },
    { "bu", "不步补布部捕哺埠簿" },
    { "ca", "擦嚓" },
    { "cai", "才彩菜采材财猜" },
    { "can", "参残餐惨蚕惭灿" },
    { "cang", "仓藏苍舱沧" },
    { "cao", "草操糙槽" },
    { "ce", "册测侧策厕册" },
    { "cen", "参岑" },
    { "ceng", "层曾蹭" },
    { "cha", "查差茶察插叉茬刹" },
    { "chai", "拆柴豺" },
    { "chan", "产缠馋蝉颤铲阐" },
    { "chang", "长场常唱昌尝肠偿畅" },
    { "chao", "超朝潮炒抄巢嘲" },
    { "che", "车彻撤扯澈" },
    { "chen", "陈晨沉尘趁辰衬" },
    { "cheng", "成城程称承诚橙惩" },
    { "chi", "吃持尺迟赤驰耻翅" },
    { "chong", "冲重虫充宠崇" },
    { "chou", "抽愁丑筹仇稠酬" },
    { "chu", "出初除处触厨储楚" },
    { "chuai", "踹揣" },
    { "chuan", "传穿船串川喘" },
    { "chuang", "创窗床闯疮" },
    { "chui", "吹垂锤炊捶" },
    { "chun", "春纯唇醇蠢" },
    { "chuo", "戳绰辍" },
    { "ci", "词此次慈磁雌辞" },
    { "cong", "从聪葱匆丛" },
    { "cou", "凑辏" },
    { "cu", "粗促醋簇猝" },
    { "cuan", "窜篡蹿" },
    { "cui", "催翠脆粹淬" },
    { "cun", "村存寸忖" },
    { "cuo", "错搓撮措挫" },
    { "da", "大达答打搭瘩" },
    { "dai", "带代待袋呆逮贷黛" },
    { "dan", "但单担蛋胆淡旦氮" },
    { "dang", "当党荡挡档铛" },
    { "dao", "到道导刀岛盗蹈" },
    { "de", "的得德地" },
    { "deng", "等灯登瞪凳邓" },
    { "di", "地第低底滴敌笛递" },
    { "dian", "点电店垫典殿淀掂" },
    { "diao", "调掉吊雕叼碟" },
    { "die", "跌叠爹碟蝶" },
    { "ding", "定顶丁订盯鼎锭" },
    { "diu", "丢" },
    { "dong", "东动栋冻懂董" },
    { "dou", "都斗豆兜痘陡" },
    { "du", "度读独堵毒赌杜渡" },
    { "duan", "段短断端锻缎" },
    { "dui", "对队堆兑敦" },
    { "dun", "顿蹲盾吨钝遁" },
    { "duo", "多夺朵惰躲跺舵" },
    { "e", "饿恶鹅额蛾厄扼" },
    { "en", "恩摁" },
    { "er", "而二儿耳尔饵" },
    { "fa", "发法罚乏阀筏" },
    { "fan", "反饭凡范翻繁返" },
    { "fang", "方放房防芳坊妨" },
    { "fei", "非飞费肥废菲沸" },
    { "fen", "分份奋坟粉氛纷" },
    { "feng", "风封峰丰逢锋枫" },
    { "fo", "佛" },
    { "fou", "否" },
    { "fu", "服福富副夫浮辅付" },
    { "ga", "嘎咖尬" },
    { "gai", "改盖该概钙丐" },
    { "gan", "干感敢杆肝竿赣" },
    { "gang", "刚钢岗港缸纲" },
    { "gao", "高告稿搞糕膏镐" },
    { "ge", "个哥歌格阁割葛" },
    { "gei", "给" },
    { "gen", "根跟亘" },
    { "geng", "更梗耕羹耿" },
    { "gong", "工公功攻供宫恭" },
    { "gou", "够勾沟狗构钩垢" },
    { "gu", "古谷股故顾估固菇" },
    { "gua", "瓜挂刮褂寡" },
    { "guai", "怪乖拐" },
    { "guan", "关官管观冠惯罐" },
    { "guang", "光广逛胱" },
    { "gui", "贵归规轨龟桂柜" },
    { "gun", "滚棍辊" },
    { "guo", "过国果锅郭裹" },
    { "ha", "哈嗨" },
    { "hai", "还海害孩骸亥" },
    { "han", "含喊汗寒汉旱憾" },
    { "hang", "行航杭吭" },
    { "hao", "好号耗豪浩郝" },
    { "he", "和河合喝贺荷赫" },
    { "hei", "黑嘿" },
    { "hen", "很恨痕" },
    { "heng", "横恒哼" },
    { "hong", "红洪宏虹鸿轰" },
    { "hou", "后候厚吼猴" },
    { "hu", "护湖虎忽互狐弧" },
    { "hua", "花化华话划哗桦" },
    { "huai", "坏怀淮槐徊" },
    { "huan", "换环欢还缓焕唤" },
    { "huang", "黄慌荒晃煌凰" },
    { "hui", "会回灰慧悔辉徽" },
    { "hun", "婚混昏魂浑" },
    { "huo", "活火或货获霍惑" },
    { "ji", "及机几急级计记鸡" },
    { "jia", "家加假价佳甲稼" },
    { "jian", "见件间坚简减剑" },
    { "jiang", "将江讲降强酱僵" },
    { "jiao", "交教角脚较娇焦" },
    { "jie", "节结解界接街捷" },
    { "jin", "进近金紧今巾筋" },
    { "jing", "经精静景竟镜敬" },
    { "jiong", "炯窘" },
    { "jiu", "就九久酒旧救纠" },
    { "ju", "局举句剧居矩聚" },
    { "juan", "卷捐圈倦绢眷" },
    { "jue", "决觉绝角掘爵诀" },
    { "jun", "军均君菌峻骏" },
    { "ka", "卡咖喀" },
    { "kai", "开凯慨楷" },
    { "kan", "看砍堪刊勘瞰" },
    { "kang", "康抗扛糠亢" },
    { "kao", "考靠烤铐" },
    { "ke", "可课科克刻客颗" },
    { "ken", "肯垦啃" },
    { "keng", "坑吭" },
    { "kong", "空孔控恐" },
    { "kou", "口扣寇抠" },
    { "ku", "苦库哭酷窟" },
    { "kua", "夸跨垮挎" },
    { "kuai", "快块筷脍" },
    { "kuan", "宽款髋" },
    { "kuang", "狂矿框眶况" },
    { "kui", "亏愧葵魁窥" },
    { "kun", "困昆坤" },
    { "kuo", "扩阔括廓" },
    { "la", "拉啦辣腊垃喇" },
    { "lai", "来赖莱睐" },
    { "lan", "兰蓝栏拦篮懒" },
    { "lang", "浪狼郎廊琅" },
    { "lao", "老劳牢落涝酪" },
    { "le", "了乐勒" },
    { "lei", "类累雷泪磊蕾" },
    { "leng", "冷愣棱" },
    { "li", "里力理立利丽粒" },
    { "lia", "俩" },
    { "lian", "连联练脸恋莲廉" },
    { "liang", "两亮量粮凉梁" },
    { "liao", "料聊疗辽僚寥" },
    { "lie", "列烈裂猎劣" },
    { "lin", "林临邻淋磷鳞" },
    { "ling", "零灵领令铃凌陵" },
    { "liu", "六流留刘柳溜榴" },
    { "long", "龙隆笼聋珑窿" },
    { "lou", "楼漏露搂陋" },
    { "lu", "路露陆炉录鹿碌" },
    { "lv", "绿驴旅虑律氯滤" },
    { "luan", "乱卵峦" },
    { "lue", "略掠" },
    { "lun", "论轮伦仑" },
    { "luo", "落罗洛骆萝螺" },
    { "ma", "吗妈马麻骂嘛码" },
    { "mai", "买麦卖迈脉埋" },
    { "man", "满慢漫蛮瞒蔓" },
    { "mang", "忙盲茫莽芒" },
    { "mao", "毛冒帽茂猫矛贸" },
    { "me", "么" },
    { "mei", "没美每妹煤眉枚" },
    { "men", "门们闷焖" },
    { "meng", "梦猛蒙盟萌锰" },
    { "mi", "米密迷蜜秘谜靡" },
    { "mian", "面棉免眠绵冕" },
    { "miao", "秒苗庙妙渺藐" },
    { "mie", "灭蔑" },
    { "min", "民敏闽珉" },
    { "ming", "明名命鸣铭螟" },
    { "miu", "谬" },
    { "mo", "模磨莫墨沫陌寞" },
    { "mou", "谋某牟眸" },
    { "mu", "木目母亩幕牧穆" },
    { "na", "那拿哪纳钠娜" },
    { "nai", "乃奶耐奈萘" },
    { "nan", "南男难喃楠" },
    { "nang", "囊" },
    { "nao", "脑闹挠恼淖" },
    { "ne", "呢" },
    { "nei", "内" },
    { "nen", "嫩" },
    { "neng", "能" },
    { "ni", "你泥尼逆匿溺" },
    { "nian", "年念粘捻碾" },
    { "niang", "娘" },
    { "niao", "鸟尿脲" },
    { "nie", "捏聂孽镍" },
    { "nin", "您" },
    { "ning", "宁凝拧泞" },
    { "niu", "牛扭纽钮" },
    { "nong", "农浓弄脓" },
    { "nu", "努怒奴" },
    { "nv", "女" },
    { "nuan", "暖" },
    { "nue", "虐疟" },
    { "nuo", "诺挪懦" },
    { "o", "哦噢喔" },
    { "ou", "欧偶呕殴" },
    { "pa", "怕爬趴帕琶啪" },
    { "pai", "派排拍牌徘" },
    { "pan", "盘判盼攀畔潘" },
    { "pang", "旁胖庞乓" },
    { "pao", "跑泡抛炮袍咆" },
    { "pei", "配培陪佩胚沛" },
    { "pen", "盆喷" },
    { "peng", "朋碰蓬棚澎膨" },
    { "pi", "皮批匹疲脾劈琵" },
    { "pian", "片偏篇骗翩" },
    { "piao", "票飘漂瓢朴" },
    { "pie", "撇瞥" },
    { "pin", "品拼贫频聘" },
    { "ping", "平评瓶凭萍苹" },
    { "po", "破坡泼迫婆魄" },
    { "pou", "剖" },
    { "pu", "普铺扑朴葡谱" },
    { "qi", "起气期七其奇棋" },
    { "qia", "恰卡掐洽" },
    { "qian", "前钱千签浅牵铅" },
    { "qiang", "强墙枪腔蔷羌" },
    { "qiao", "桥巧敲悄翘瞧侨" },
    { "qie", "切且怯茄窃" },
    { "qin", "亲琴勤秦芹钦禽" },
    { "qing", "情请清青轻倾晴" },
    { "qiong", "穷琼" },
    { "qiu", "求秋球丘蚯囚" },
    { "qu", "去区取曲趋躯渠" },
    { "quan", "全权圈泉拳犬券" },
    { "que", "却确雀缺鹊" },
    { "qun", "群裙" },
    { "ran", "然染燃冉" },
    { "rang", "让嚷瓤" },
    { "rao", "绕饶扰" },
    { "re", "热" },
    { "ren", "人认任忍刃仁" },
    { "reng", "仍扔" },
    { "ri", "日" },
    { "rong", "容荣融绒熔溶" },
    { "rou", "肉柔揉" },
    { "ru", "如入儒乳辱蠕" },
    { "ruan", "软阮" },
    { "rui", "瑞锐蕊睿" },
    { "run", "润闰" },
    { "ruo", "若弱" },
    { "sa", "撒萨洒" },
    { "sai", "赛塞腮" },
    { "san", "三散伞叁" },
    { "sang", "桑丧嗓" },
    { "sao", "扫骚臊" },
    { "se", "色瑟涩塞" },
    { "sen", "森" },
    { "seng", "僧" },
    { "sha", "沙杀纱啥煞杉砂" },
    { "shai", "晒筛" },
    { "shan", "山闪善扇衫珊删" },
    { "shang", "上商伤赏晌裳" },
    { "shao", "少烧勺哨梢捎" },
    { "she", "设社射蛇舍涉奢" },
    { "shen", "身深神甚审肾绅" },
    { "sheng", "生声胜升省圣盛" },
    { "shi", "是时十事式实师" },
    { "shou", "手收首受守寿兽" },
    { "shu", "书数树术输叔舒" },
    { "shua", "刷耍" },
    { "shuai", "帅摔甩衰" },
    { "shuan", "栓拴" },
    { "shuang", "双霜爽" },
    { "shui", "水睡税谁" },
    { "shun", "顺瞬舜" },
    { "shuo", "说硕烁朔" },
    { "si", "四思斯丝死私寺" },
    { "song", "送松耸颂宋诵" },
    { "sou", "搜嗽艘擞" },
    { "su", "苏素速诉塑宿俗" },
    { "suan", "算酸蒜" },
    { "sui", "随岁碎虽穗隋" },
    { "sun", "孙损笋隼" },
    { "suo", "所锁索缩梭" },
    { "ta", "他她它踏塔塌獭" },
    { "tai", "太台态泰抬苔胎" },
    { "tan", "谈探碳摊弹坛坦" },
    { "tang", "堂汤糖躺趟塘棠" },
    { "tao", "套逃桃陶淘萄涛" },
    { "te", "特" },
    { "teng", "疼腾藤" },
    { "ti", "提体题替梯踢蹄" },
    { "tian", "天田填甜添佃" },
    { "tiao", "条跳挑调迢" },
    { "tie", "贴铁帖" },
    { "ting", "听停庭厅挺艇" },
    { "tong", "同通痛铜童桐" },
    { "tou", "头投透偷" },
    { "tu", "图土突途涂秃屠" },
    { "tuan", "团湍" },
    { "tui", "推退腿蜕颓" },
    { "tun", "吞屯臀囤" },
    { "tuo", "脱托拖拓陀驼" },
    { "wa", "挖瓦蛙哇洼" },
    { "wai", "外歪" },
    { "wan", "万完晚玩弯婉腕" },
    { "wang", "王望往忘网旺" },
    { "wei", "为位微未伟威卫" },
    { "wen", "问文温稳闻纹瘟" },
    { "weng", "翁嗡" },
    { "wo", "我卧窝握斡" },
    { "wu", "无五物务武伍屋" },
    { "xi", "西习系细喜息惜溪" },
    { "xia", "下夏霞吓狭峡瞎" },
    { "xian", "先现线县显险贤" },
    { "xiang", "想向象香乡祥享" },
    { "xiao", "小效校笑消晓萧" },
    { "xie", "些写谢鞋协斜蟹" },
    { "xin", "心新信欣辛芯锌" },
    { "xing", "行性星型兴刑形" },
    { "xiong", "雄熊胸汹" },
    { "xiu", "修休秀锈袖羞嗅" },
    { "xu", "需须序续虚徐畜" },
    { "xuan", "选悬旋宣玄眩萱" },
    { "xue", "学雪血穴靴削" },
    { "xun", "寻讯训迅循巡勋" },
    { "ya", "呀压亚牙芽雅哑" },
    { "yan", "言眼演严沿炎宴" },
    { "yang", "样阳扬羊洋氧秧" },
    { "yao", "要药摇腰遥窑妖" },
    { "ye", "也业夜叶野爷冶" },
    { "yi", "一以已意依医仪" },
    { "yin", "因音银引隐阴印" },
    { "ying", "应英影硬迎鹰盈" },
    { "yo", "哟" },
    { "yong", "用永勇涌拥佣" },
    { "you", "有又由右优游友" },
    { "yu", "与于雨语鱼遇预" },
    { "yuan", "元远院圆原缘源" },
    { "yue", "月越约悦跃岳乐" },
    { "yun", "云运允晕韵孕耘" },
    { "za", "杂砸扎" },
    { "zai", "在再载灾栽宰" },
    { "zan", "赞暂咱攒" },
    { "zang", "脏葬藏赃" },
    { "zao", "早造遭澡噪灶凿" },
    { "ze", "则责择泽" },
    { "zei", "贼" },
    { "zen", "怎" },
    { "zeng", "增赠憎" },
    { "zha", "扎炸查闸眨榨喳" },
    { "zhai", "摘窄债寨" },
    { "zhan", "站展战占沾斩盏" },
    { "zhang", "长张掌涨帐仗樟" },
    { "zhao", "找照招着朝兆罩" },
    { "zhe", "着这者折遮蔗" },
    { "zhen", "真阵镇震珍枕侦" },
    { "zheng", "正争证整政挣蒸" },
    { "zhi", "只知指志直质制" },
    { "zhong", "中种重众钟忠终" },
    { "zhou", "周州洲轴皱粥宙" },
    { "zhu", "主住助注筑珠祝" },
    { "zhua", "抓爪" },
    { "zhuai", "拽" },
    { "zhuan", "转专砖赚传篆" },
    { "zhuang", "装状撞庄桩" },
    { "zhui", "追坠锥椎" },
    { "zhun", "准谆" },
    { "zhuo", "着捉桌卓琢浊" },
    { "zi", "子自字资紫滋姿" },
    { "zong", "总宗综纵棕" },
    { "zou", "走奏邹" },
    { "zu", "组祖足族阻租" },
    { "zuan", "钻纂" },
    { "zui", "最嘴醉罪" },
    { "zun", "尊遵" },
    { "zuo", "做坐作左座昨佐" },

    { NULL, NULL }
};
#endif

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
lv_obj_t * lv_ime_pinyin_create(lv_obj_t * parent)
{
    LV_LOG_INFO("begin");
    lv_obj_t * obj = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(obj);
    return obj;
}

/*=====================
 * Setter functions
 *====================*/

void lv_ime_pinyin_set_keyboard(lv_obj_t * obj, lv_obj_t * kb)
{
    if(kb) {
        LV_ASSERT_OBJ(kb, &lv_keyboard_class);
    }

    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->kb = kb;
    lv_obj_set_parent(obj, lv_obj_get_parent(kb));
    lv_obj_set_parent(pinyin_ime->cand_panel, lv_obj_get_parent(kb));
    lv_obj_add_event_cb(pinyin_ime->kb, lv_ime_pinyin_kb_event, LV_EVENT_VALUE_CHANGED, obj);
    lv_obj_align_to(pinyin_ime->cand_panel, pinyin_ime->kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
}

void lv_ime_pinyin_set_dict(lv_obj_t * obj, lv_pinyin_dict_t * dict)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    init_pinyin_dict(obj, dict);
}

void lv_ime_pinyin_set_mode(lv_obj_t * obj, lv_ime_pinyin_mode_t mode)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    LV_ASSERT_OBJ(pinyin_ime->kb, &lv_keyboard_class);

    pinyin_ime->mode = mode;

#if LV_IME_PINYIN_USE_K9_MODE
    if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
        pinyin_k9_init_data(obj);
        lv_keyboard_set_map(pinyin_ime->kb, LV_KEYBOARD_MODE_USER_1, (const char **)lv_btnm_def_pinyin_k9_map,
                            default_kb_ctrl_k9_map);
        lv_keyboard_set_mode(pinyin_ime->kb, LV_KEYBOARD_MODE_USER_1);
    }
#endif
}

/*=====================
 * Getter functions
 *====================*/

lv_obj_t * lv_ime_pinyin_get_kb(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->kb;
}

lv_obj_t * lv_ime_pinyin_get_cand_panel(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->cand_panel;
}

const lv_pinyin_dict_t * lv_ime_pinyin_get_dict(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    return pinyin_ime->dict;
}

/*=====================
 * Other functions
 *====================*/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void lv_ime_pinyin_constructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t py_str_i = 0;
    uint16_t btnm_i = 0;
    for(btnm_i = 0; btnm_i < (LV_IME_PINYIN_CAND_TEXT_NUM + 3); btnm_i++) {
        if(btnm_i == 0) {
            lv_btnm_def_pinyin_sel_map[btnm_i] = "<";
        }
        else if(btnm_i == (LV_IME_PINYIN_CAND_TEXT_NUM + 1)) {
            lv_btnm_def_pinyin_sel_map[btnm_i] = ">";
        }
        else if(btnm_i == (LV_IME_PINYIN_CAND_TEXT_NUM + 2)) {
            lv_btnm_def_pinyin_sel_map[btnm_i] = "";
        }
        else {
            lv_pinyin_cand_str[py_str_i][0] = ' ';
            lv_btnm_def_pinyin_sel_map[btnm_i] = lv_pinyin_cand_str[py_str_i];
            py_str_i++;
        }
    }

    pinyin_ime->mode = LV_IME_PINYIN_MODE_K26;
    pinyin_ime->py_page = 0;
    pinyin_ime->ta_count = 0;
    pinyin_ime->cand_num = 0;
    lv_memzero(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
    lv_memzero(pinyin_ime->py_num, sizeof(pinyin_ime->py_num));
    lv_memzero(pinyin_ime->py_pos, sizeof(pinyin_ime->py_pos));

    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

#if LV_IME_PINYIN_USE_DEFAULT_DICT
    init_pinyin_dict(obj, lv_ime_pinyin_def_dict);
#endif

    /* Init pinyin_ime->cand_panel */
    pinyin_ime->cand_panel = lv_buttonmatrix_create(lv_obj_get_parent(obj));
    lv_buttonmatrix_set_map(pinyin_ime->cand_panel, (const char **)lv_btnm_def_pinyin_sel_map);
    lv_obj_set_size(pinyin_ime->cand_panel, LV_PCT(100), LV_PCT(5));
    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);

    lv_buttonmatrix_set_one_checked(pinyin_ime->cand_panel, true);
    lv_obj_remove_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    /* Set cand_panel style*/
    // Default style
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_pad_all(pinyin_ime->cand_panel, 8, 0);
    lv_obj_set_style_pad_gap(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_radius(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_pad_gap(pinyin_ime->cand_panel, 0, 0);
    lv_obj_set_style_base_dir(pinyin_ime->cand_panel, LV_BASE_DIR_LTR, 0);

    // LV_PART_ITEMS style
    lv_obj_set_style_radius(pinyin_ime->cand_panel, 12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(pinyin_ime->cand_panel, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_opa(pinyin_ime->cand_panel, LV_OPA_0, LV_PART_ITEMS);

    // LV_PART_ITEMS | LV_STATE_PRESSED style
    lv_obj_set_style_bg_opa(pinyin_ime->cand_panel, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(pinyin_ime->cand_panel, lv_color_white(), LV_PART_ITEMS | LV_STATE_PRESSED);

    /* event handler */
    lv_obj_add_event_cb(pinyin_ime->cand_panel, lv_ime_pinyin_cand_panel_event, LV_EVENT_VALUE_CHANGED, obj);
    lv_obj_add_event_cb(obj, lv_ime_pinyin_style_change_event, LV_EVENT_STYLE_CHANGED, NULL);

#if LV_IME_PINYIN_USE_K9_MODE
    pinyin_ime->k9_input_str_len = 0;
    pinyin_ime->k9_py_ll_pos = 0;
    pinyin_ime->k9_legal_py_count = 0;
    lv_memzero(pinyin_ime->k9_input_str, LV_IME_PINYIN_K9_MAX_INPUT);

    pinyin_k9_init_data(obj);

    lv_ll_init(&(pinyin_ime->k9_legal_py_ll), sizeof(ime_pinyin_k9_py_str_t));
#endif
}

static void lv_ime_pinyin_destructor(const lv_obj_class_t * class_p, lv_obj_t * obj)
{
    LV_UNUSED(class_p);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(lv_obj_is_valid(pinyin_ime->kb))
        lv_obj_delete(pinyin_ime->kb);

    if(lv_obj_is_valid(pinyin_ime->cand_panel))
        lv_obj_delete(pinyin_ime->cand_panel);
}

static void lv_ime_pinyin_kb_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * kb = lv_event_get_current_target(e);
    lv_obj_t * obj = lv_event_get_user_data(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

#if LV_IME_PINYIN_USE_K9_MODE
    static const char * k9_py_map[8] = {"abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
#endif

    if(code == LV_EVENT_VALUE_CHANGED) {
        uint16_t btn_id  = lv_buttonmatrix_get_selected_button(kb);
        if(btn_id == LV_BUTTONMATRIX_BUTTON_NONE) return;

        const char * txt = lv_buttonmatrix_get_button_text(kb, lv_buttonmatrix_get_selected_button(kb));
        if(txt == NULL) return;

        lv_obj_t * ta = lv_keyboard_get_textarea(pinyin_ime->kb);

#if LV_IME_PINYIN_USE_K9_MODE
        if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {

            uint16_t tmp_button_str_len = lv_strlen(pinyin_ime->input_char);
            if((btn_id >= 16) && (tmp_button_str_len > 0) && (btn_id < (16 + LV_IME_PINYIN_K9_CAND_TEXT_NUM))) {
                lv_memzero(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));
                lv_strcat(pinyin_ime->input_char, txt);
                pinyin_input_proc(obj);

                for(int index = 0; index < (pinyin_ime->ta_count + tmp_button_str_len); index++) {
                    lv_textarea_delete_char(ta);
                }

                pinyin_ime->ta_count = tmp_button_str_len;
                pinyin_ime->k9_input_str_len = tmp_button_str_len;
                lv_textarea_add_text(ta, pinyin_ime->input_char);

                return;
            }
        }
#endif

        if(lv_strcmp(txt, "Enter") == 0 || lv_strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
            pinyin_ime_clear_data(obj);
            lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else if(lv_strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
            // del input char
            if(pinyin_ime->ta_count > 0) {
                if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26)
                    pinyin_ime->input_char[pinyin_ime->ta_count - 1] = '\0';
#if LV_IME_PINYIN_USE_K9_MODE
                else
                    pinyin_ime->k9_input_str[pinyin_ime->ta_count - 1] = '\0';
#endif

                pinyin_ime->ta_count--;
                if(pinyin_ime->ta_count <= 0) {
                    pinyin_ime_clear_data(obj);
                    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
                }
                else if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26) {
                    pinyin_input_proc(obj);
                }
#if LV_IME_PINYIN_USE_K9_MODE
                else if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
                    pinyin_ime->k9_input_str_len = lv_strlen(pinyin_ime->input_char) - 1;
                    pinyin_k9_get_legal_py(obj, pinyin_ime->k9_input_str, k9_py_map);
                    pinyin_k9_fill_cand(obj);
                    pinyin_input_proc(obj);
                    pinyin_ime->ta_count--;
                }
#endif
            }
        }
        else if((lv_strcmp(txt, "ABC") == 0) || (lv_strcmp(txt, "abc") == 0) || (lv_strcmp(txt, "1#") == 0) ||
                (lv_strcmp(txt, LV_SYMBOL_OK) == 0)) {
            pinyin_ime_clear_data(obj);
            return;
        }
        else if(lv_strcmp(txt, "123") == 0) {
            for(uint16_t i = 0; i < lv_strlen(txt); i++)
                lv_textarea_delete_char(ta);

            pinyin_ime_clear_data(obj);
            lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
            lv_ime_pinyin_set_mode(obj, LV_IME_PINYIN_MODE_K9_NUMBER);
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
            lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
        else if(lv_strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
            if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K26) {
                lv_ime_pinyin_set_mode(obj, LV_IME_PINYIN_MODE_K9);
            }
            else if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
                lv_ime_pinyin_set_mode(obj, LV_IME_PINYIN_MODE_K26);
                lv_keyboard_set_mode(pinyin_ime->kb, LV_KEYBOARD_MODE_TEXT_LOWER);
            }
            else if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9_NUMBER) {
                lv_ime_pinyin_set_mode(obj, LV_IME_PINYIN_MODE_K9);
            }
            pinyin_ime_clear_data(obj);
        }
        else if((pinyin_ime->mode == LV_IME_PINYIN_MODE_K26) && ((txt[0] >= 'a' && txt[0] <= 'z') || (txt[0] >= 'A' &&
                                                                                                      txt[0] <= 'Z'))) {
            uint16_t len = lv_strlen(pinyin_ime->input_char);
            lv_snprintf(pinyin_ime->input_char + len, sizeof(pinyin_ime->input_char) - len, "%s", txt);
            pinyin_input_proc(obj);
            pinyin_ime->ta_count++;
        }
#if LV_IME_PINYIN_USE_K9_MODE
        else if((pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) && (txt[0] >= 'a' && txt[0] <= 'z')) {
            for(uint16_t i = 0; i < 8; i++) {
                if((lv_strcmp(txt, k9_py_map[i]) == 0) || (lv_strcmp(txt, "abc ") == 0)) {
                    if(lv_strcmp(txt, "abc ") == 0)    pinyin_ime->k9_input_str_len += lv_strlen(k9_py_map[i]) + 1;
                    else                            pinyin_ime->k9_input_str_len += lv_strlen(k9_py_map[i]);
                    pinyin_ime->k9_input_str[pinyin_ime->ta_count] = 50 + i;
                    pinyin_ime->k9_input_str[pinyin_ime->ta_count + 1] = '\0';

                    break;
                }
            }
            pinyin_k9_get_legal_py(obj, pinyin_ime->k9_input_str, k9_py_map);
            pinyin_k9_fill_cand(obj);
            pinyin_input_proc(obj);
        }
        else if(lv_strcmp(txt, LV_SYMBOL_LEFT) == 0) {
            pinyin_k9_cand_page_proc(obj, 0);
        }
        else if(lv_strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
            pinyin_k9_cand_page_proc(obj, 1);
        }
#endif
    }
}

static void lv_ime_pinyin_cand_panel_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * cand_panel = lv_event_get_current_target(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_user_data(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * ta = lv_keyboard_get_textarea(pinyin_ime->kb);
        if(ta == NULL) return;

        uint32_t id = lv_buttonmatrix_get_selected_button(cand_panel);
        if(id == LV_BUTTONMATRIX_BUTTON_NONE) {
            return;
        }
        else if(id == 0) {
            pinyin_page_proc(obj, 0);
            return;
        }
        else if(id == (LV_IME_PINYIN_CAND_TEXT_NUM + 1)) {
            pinyin_page_proc(obj, 1);
            return;
        }

        const char * txt = lv_buttonmatrix_get_button_text(cand_panel, id);
        uint16_t index = 0;
        for(index = 0; index < pinyin_ime->ta_count; index++)
            lv_textarea_delete_char(ta);

        lv_textarea_add_text(ta, txt);

        pinyin_ime_clear_data(obj);
    }
}

static void pinyin_input_proc(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    pinyin_ime->cand_str = pinyin_search_matching(obj, pinyin_ime->input_char, &pinyin_ime->cand_num);
    if(pinyin_ime->cand_str == NULL) {
        return;
    }

    pinyin_ime->py_page = 0;

    for(uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
        lv_memset(lv_pinyin_cand_str[i], 0x00, sizeof(lv_pinyin_cand_str[i]));
        lv_pinyin_cand_str[i][0] = ' ';
    }

    // fill buf
    for(uint8_t i = 0; (i < pinyin_ime->cand_num && i < LV_IME_PINYIN_CAND_TEXT_NUM); i++) {
        for(uint8_t j = 0; j < 3; j++) {
            lv_pinyin_cand_str[i][j] = pinyin_ime->cand_str[i * 3 + j];
        }
    }

    lv_obj_remove_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(pinyin_ime->cand_panel);
}

static void pinyin_page_proc(lv_obj_t * obj, uint16_t dir)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;
    uint16_t page_num = pinyin_ime->cand_num / LV_IME_PINYIN_CAND_TEXT_NUM;
    uint16_t remainder = pinyin_ime->cand_num % LV_IME_PINYIN_CAND_TEXT_NUM;

    if(!pinyin_ime->cand_str) return;

    if(dir == 0) {
        if(pinyin_ime->py_page) {
            pinyin_ime->py_page--;
        }
    }
    else {
        if(remainder == 0) {
            page_num -= 1;
        }
        if(pinyin_ime->py_page < page_num) {
            pinyin_ime->py_page++;
        }
        else return;
    }

    for(uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
        lv_memset(lv_pinyin_cand_str[i], 0x00, sizeof(lv_pinyin_cand_str[i]));
        lv_pinyin_cand_str[i][0] = ' ';
    }

    // fill buf
    uint16_t offset = pinyin_ime->py_page * (3 * LV_IME_PINYIN_CAND_TEXT_NUM);
    for(uint8_t i = 0; (i < pinyin_ime->cand_num && i < LV_IME_PINYIN_CAND_TEXT_NUM); i++) {
        if((remainder > 0) && (pinyin_ime->py_page == page_num)) {
            if(i >= remainder)
                break;
        }
        for(uint8_t j = 0; j < 3; j++) {
            lv_pinyin_cand_str[i][j] = pinyin_ime->cand_str[offset + (i * 3) + j];
        }
    }
}

static void lv_ime_pinyin_style_change_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_current_target(e);

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    if(code == LV_EVENT_STYLE_CHANGED) {
        const lv_font_t * font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        lv_obj_set_style_text_font(pinyin_ime->cand_panel, font, 0);
    }
}

static void init_pinyin_dict(lv_obj_t * obj, const lv_pinyin_dict_t * dict)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    char headletter = 'a';
    uint16_t offset_sum = 0;
    uint16_t offset_count = 0;
    uint16_t letter_calc = 0;

    pinyin_ime->dict = dict;

    for(uint16_t i = 0; ; i++) {
        if((NULL == (dict[i].py)) || (NULL == (dict[i].py_mb))) {
            headletter = dict[i - 1].py[0];
            letter_calc = headletter - 'a';
            pinyin_ime->py_num[letter_calc] = offset_count;
            break;
        }

        if(headletter == (dict[i].py[0])) {
            offset_count++;
        }
        else {
            headletter = dict[i].py[0];
            pinyin_ime->py_num[letter_calc] = offset_count;
            letter_calc = headletter - 'a';
            offset_sum += offset_count;
            pinyin_ime->py_pos[letter_calc] = offset_sum;

            offset_count = 1;
        }
    }
}

static char * pinyin_search_matching(lv_obj_t * obj, char * py_str, uint16_t * cand_num)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    const lv_pinyin_dict_t * cpHZ;
    uint8_t index, len = 0, offset;
    volatile uint8_t count = 0;

    if(*py_str == '\0')    return NULL;
    if(*py_str == 'i')     return NULL;
    if(*py_str == 'u')     return NULL;
    if(*py_str == 'v')     return NULL;
    if(*py_str == ' ')     return NULL;

    offset = py_str[0] - 'a';
    len = lv_strlen(py_str);

    cpHZ  = &pinyin_ime->dict[pinyin_ime->py_pos[offset]];
    count = pinyin_ime->py_num[offset];

    while(count--) {
        for(index = 0; index < len; index++) {
            if(*(py_str + index) != *((cpHZ->py) + index)) {
                break;
            }
        }

        // perfect match
        if(len == 1 || index == len) {
            // The Chinese character in UTF-8 encoding format is 3 bytes
            * cand_num = lv_strlen((const char *)(cpHZ->py_mb)) / 3;
            return (char *)(cpHZ->py_mb);
        }
        cpHZ++;
    }
    return NULL;
}

static void pinyin_ime_clear_data(lv_obj_t * obj)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

#if LV_IME_PINYIN_USE_K9_MODE
    if(pinyin_ime->mode == LV_IME_PINYIN_MODE_K9) {
        pinyin_ime->k9_input_str_len = 0;
        pinyin_ime->k9_py_ll_pos = 0;
        pinyin_ime->k9_legal_py_count = 0;
        lv_memzero(pinyin_ime->k9_input_str,  LV_IME_PINYIN_K9_MAX_INPUT);
        lv_memzero(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        for(uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
            lv_strcpy(lv_pinyin_k9_cand_str[i], " ");
        }
        lv_strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        lv_strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
        lv_buttonmatrix_set_map(pinyin_ime->kb, (const char **)lv_btnm_def_pinyin_k9_map);
    }
#endif

    pinyin_ime->ta_count = 0;
    for(uint8_t i = 0; i < LV_IME_PINYIN_CAND_TEXT_NUM; i++) {
        lv_memset(lv_pinyin_cand_str[i], 0x00, sizeof(lv_pinyin_cand_str[i]));
        lv_pinyin_cand_str[i][0] = ' ';
    }
    lv_memzero(pinyin_ime->input_char, sizeof(pinyin_ime->input_char));

    lv_obj_add_flag(pinyin_ime->cand_panel, LV_OBJ_FLAG_HIDDEN);
}

#if LV_IME_PINYIN_USE_K9_MODE
static void pinyin_k9_init_data(lv_obj_t * obj)
{
    LV_UNUSED(obj);

    uint16_t py_str_i = 0;
    uint16_t btnm_i = 0;
    for(btnm_i = 19; btnm_i < (LV_IME_PINYIN_K9_CAND_TEXT_NUM + 21); btnm_i++) {
        if(py_str_i == LV_IME_PINYIN_K9_CAND_TEXT_NUM) {
            lv_strcpy(lv_pinyin_k9_cand_str[py_str_i], LV_SYMBOL_RIGHT"\0");
        }
        else if(py_str_i == LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1) {
            lv_strcpy(lv_pinyin_k9_cand_str[py_str_i], "\0");
        }
        else {
            lv_strcpy(lv_pinyin_k9_cand_str[py_str_i], " \0");
        }

        lv_btnm_def_pinyin_k9_map[btnm_i] = lv_pinyin_k9_cand_str[py_str_i];
        py_str_i++;
    }

    default_kb_ctrl_k9_map[0]  = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
    default_kb_ctrl_k9_map[1]  = LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | 1;
    default_kb_ctrl_k9_map[4]  = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
    default_kb_ctrl_k9_map[5]  = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
    default_kb_ctrl_k9_map[9]  = LV_KEYBOARD_CTRL_BUTTON_FLAGS | 1;
    default_kb_ctrl_k9_map[10] = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
    default_kb_ctrl_k9_map[14] = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
    default_kb_ctrl_k9_map[15] = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
    default_kb_ctrl_k9_map[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 16] = LV_BUTTONMATRIX_CTRL_CHECKED | 1;
}

static void pinyin_k9_get_legal_py(lv_obj_t * obj, char * k9_input, const char * py9_map[])
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    uint16_t len = lv_strlen(k9_input);

    if((len == 0) || (len >= LV_IME_PINYIN_K9_MAX_INPUT)) {
        return;
    }

    char py_comp[LV_IME_PINYIN_K9_MAX_INPUT] = {0};
    int mark[LV_IME_PINYIN_K9_MAX_INPUT] = {0};
    int index = 0;
    int flag = 0;
    uint16_t count = 0;

    uint32_t ll_len = 0;
    ime_pinyin_k9_py_str_t * ll_index = NULL;

    ll_len = lv_ll_get_len(&pinyin_ime->k9_legal_py_ll);
    ll_index = lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);

    while(index != -1) {
        if(index == len) {
            if(pinyin_k9_is_valid_py(obj, py_comp)) {
                if((count >= ll_len) || (ll_len == 0)) {
                    ll_index = lv_ll_ins_tail(&pinyin_ime->k9_legal_py_ll);
                    lv_strcpy(ll_index->py_str, py_comp);
                }
                else if((count < ll_len)) {
                    lv_strcpy(ll_index->py_str, py_comp);
                    ll_index = lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index);
                }
                count++;
            }
            index--;
        }
        else {
            flag = mark[index];
            if((size_t)flag < lv_strlen(py9_map[k9_input[index] - '2'])) {
                py_comp[index] = py9_map[k9_input[index] - '2'][flag];
                mark[index] = mark[index] + 1;
                index++;
            }
            else {
                mark[index] = 0;
                index--;
            }
        }
    }

    if(count > 0) {
        pinyin_ime->ta_count++;
        pinyin_ime->k9_legal_py_count = count;
    }
}

/*true: visible; false: not visible*/
static bool pinyin_k9_is_valid_py(lv_obj_t * obj, char * py_str)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    const lv_pinyin_dict_t * cpHZ = NULL;
    uint8_t index = 0, len = 0, offset = 0;
    volatile uint8_t count = 0;

    if(*py_str == '\0')    return false;
    if(*py_str == 'i')     return false;
    if(*py_str == 'u')     return false;
    if(*py_str == 'v')     return false;

    offset = py_str[0] - 'a';
    len = lv_strlen(py_str);

    cpHZ  = &pinyin_ime->dict[pinyin_ime->py_pos[offset]];
    count = pinyin_ime->py_num[offset];

    while(count--) {
        for(index = 0; index < len; index++) {
            if(*(py_str + index) != *((cpHZ->py) + index)) {
                break;
            }
        }

        // perfect match
        if(len == 1 || index == len) {
            return true;
        }
        cpHZ++;
    }
    return false;
}

static void pinyin_k9_fill_cand(lv_obj_t * obj)
{
    uint16_t index = 0, tmp_len = 0;
    ime_pinyin_k9_py_str_t * ll_index = NULL;

    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    tmp_len = pinyin_ime->k9_legal_py_count;

    if(tmp_len != cand_len) {
        lv_memzero(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        lv_strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        lv_strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");
        cand_len = tmp_len;
    }

    ll_index = lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);
    lv_strcpy(pinyin_ime->input_char, ll_index->py_str);

    for(uint8_t i = 0; i < LV_IME_PINYIN_K9_CAND_TEXT_NUM; i++) {
        lv_strcpy(lv_pinyin_k9_cand_str[i], " ");
    }

    while(ll_index) {
        if(index >= LV_IME_PINYIN_K9_CAND_TEXT_NUM)
            break;

        if(index < pinyin_ime->k9_legal_py_count) {
            lv_strcpy(lv_pinyin_k9_cand_str[index], ll_index->py_str);
        }

        ll_index = lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
        index++;
    }
    pinyin_ime->k9_py_ll_pos = index;

    lv_obj_t * ta = lv_keyboard_get_textarea(pinyin_ime->kb);
    for(index = 0; index < pinyin_ime->k9_input_str_len; index++) {
        lv_textarea_delete_char(ta);
    }
    pinyin_ime->k9_input_str_len = lv_strlen(pinyin_ime->input_char);
    lv_textarea_add_text(ta, pinyin_ime->input_char);
}

static void pinyin_k9_cand_page_proc(lv_obj_t * obj, uint16_t dir)
{
    lv_ime_pinyin_t * pinyin_ime = (lv_ime_pinyin_t *)obj;

    lv_obj_t * ta = lv_keyboard_get_textarea(pinyin_ime->kb);
    uint16_t ll_len =  lv_ll_get_len(&pinyin_ime->k9_legal_py_ll);

    if((ll_len > LV_IME_PINYIN_K9_CAND_TEXT_NUM) && (pinyin_ime->k9_legal_py_count > LV_IME_PINYIN_K9_CAND_TEXT_NUM)) {
        ime_pinyin_k9_py_str_t * ll_index = NULL;
        int count = 0;

        ll_index = lv_ll_get_head(&pinyin_ime->k9_legal_py_ll);
        while(ll_index) {
            if(count >= pinyin_ime->k9_py_ll_pos)   break;

            ll_index = lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
            count++;
        }

        if((NULL == ll_index) && (dir == 1))   return;

        lv_memzero(lv_pinyin_k9_cand_str, sizeof(lv_pinyin_k9_cand_str));
        lv_strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM], LV_SYMBOL_RIGHT"\0");
        lv_strcpy(lv_pinyin_k9_cand_str[LV_IME_PINYIN_K9_CAND_TEXT_NUM + 1], "\0");

        // next page
        if(dir == 1) {
            for(uint8_t i = 0; i < LV_IME_PINYIN_K9_CAND_TEXT_NUM; i++) {
                lv_strcpy(lv_pinyin_k9_cand_str[i], " ");
            }

            count = 0;
            while(ll_index) {
                if(count >= (LV_IME_PINYIN_K9_CAND_TEXT_NUM - 1))
                    break;

                lv_strcpy(lv_pinyin_k9_cand_str[count], ll_index->py_str);
                ll_index = lv_ll_get_next(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the next list*/
                count++;
            }
            pinyin_ime->k9_py_ll_pos += count - 1;

        }
        // previous page
        else {
            for(uint8_t i = 0; i < LV_IME_PINYIN_K9_CAND_TEXT_NUM; i++) {
                lv_strcpy(lv_pinyin_k9_cand_str[i], " ");
            }
            count = LV_IME_PINYIN_K9_CAND_TEXT_NUM - 1;
            ll_index = lv_ll_get_prev(&pinyin_ime->k9_legal_py_ll, ll_index);
            while(ll_index) {
                if(count < 0)  break;

                lv_strcpy(lv_pinyin_k9_cand_str[count], ll_index->py_str);
                ll_index = lv_ll_get_prev(&pinyin_ime->k9_legal_py_ll, ll_index); /*Find the previous list*/
                count--;
            }

            if(pinyin_ime->k9_py_ll_pos > LV_IME_PINYIN_K9_CAND_TEXT_NUM)
                pinyin_ime->k9_py_ll_pos -= 1;
        }

        lv_textarea_set_cursor_pos(ta, LV_TEXTAREA_CURSOR_LAST);
    }
}

#endif  /*LV_IME_PINYIN_USE_K9_MODE*/

#endif  /*LV_USE_IME_PINYIN*/
