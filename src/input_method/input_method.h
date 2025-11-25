#ifndef INPUT_METHOD_H
#define INPUT_METHOD_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <set>

// 输入模式枚举
enum InputMode
{
    MODE_CHS, // Chinese Pinyin
    MODE_ENG, // English
    MODE_NUM  // Numeric
};

// 全局状态（在实现文件中定义）
extern InputMode inputMode;
extern String inputBuffer;             // Final content buffer
extern String pinyinBuffer;            // Pinyin composition buffer
extern int candidateIndex;             // Current selected candidate index
extern std::vector<String> candidates; // List of character candidates
extern bool composing;                 // True if in Pinyin composition mode

// 键盘/拼音映射
extern const char *keymap[10];
extern const char *pinyinKeymap[10];

// 拼音字典映射
extern std::map<String, std::vector<String>> py2hz;

// 多字组合相关
extern std::vector<String> multiCharBuffer;
extern String tempPinyinBuffer;
extern bool autoCommitMode;

// T9对应表显示相关
extern bool showT9Table;
extern char lastStarKey;
extern unsigned long lastStarTime;

// 自动学习/频率
extern std::map<String, int> charFrequency;
extern const String FREQ_FILE;
extern const int MAX_FREQ_ENTRIES;

// 函数接口
void loadPinyinDict();
String removeTones(const String &pinyin);
std::vector<std::vector<String>> segmentPinyin(const String &pinyin);
std::vector<String> generateMultiCharCandidates(const std::vector<std::vector<String>> &segments);
void generateCombinations(const std::vector<std::vector<String>> &charOptions,
                          int index, const String &current,
                          std::vector<String> &results);

void updateCandidates();
void handlePinyinInput(char key);
void commitCandidate();
void handleEnglishInput(char key);

// 自动学习功能
void loadFrequencyData();
void saveFrequencyData();
void updateCharFrequency(const String &character);
std::vector<String> sortCandidatesByFrequency(const std::vector<String> &rawCandidates);

#endif // INPUT_METHOD_H
