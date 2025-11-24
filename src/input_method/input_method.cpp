#include "input_method.h"

#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <algorithm>
#include "debug.h"

// 全局状态定义
InputMode inputMode = MODE_CHS;
String inputBuffer = "";        // Final content buffer
String pinyinBuffer = "";       // Pinyin composition buffer
int candidateIndex = 0;         // Current selected candidate index
std::vector<String> candidates; // List of character candidates
bool composing = false;         // True if in Pinyin composition mode

// 九宫格映射（数字 -> T9英文字符）
const char *keymap[10] = {
    "",     // 0
    "",     // 1
    "abc",  // 2
    "def",  // 3
    "ghi",  // 4
    "jkl",  // 5
    "mno",  // 6
    "pqrs", // 7
    "tuv",  // 8
    "wxyz"  // 9
};

// T9数字键到拼音字母映射（用于中文输入）
const char *pinyinKeymap[10] = {
    "",     // 0 - 空格或确认
    "",     // 1 - 不使用
    "abc",  // 2
    "def",  // 3
    "ghi",  // 4
    "jkl",  // 5
    "mno",  // 6
    "pqrs", // 7
    "tuv",  // 8
    "wxyz"  // 9
};

// 拼音转汉字映射（外部大词库，开机时加载）
std::map<String, std::vector<String>> py2hz;

// 多字组合相关变量
std::vector<String> multiCharBuffer; // 存储已组合的字符
String tempPinyinBuffer = "";        // 临时拼音缓冲区
bool autoCommitMode = true;          // 自动提交模式

// T9对应表显示相关变量
bool showT9Table = false; // 是否显示T9对应表
char lastStarKey = 0;
unsigned long lastStarTime = 0;

// 自动学习功能相关变量
std::map<String, int> charFrequency;       // 字符使用频率统计
const String FREQ_FILE = "/frequency.txt"; // 频率数据文件
const int MAX_FREQ_ENTRIES = 500;          // 最大频率记录数量

// 拼音字典加载
void loadPinyinDict()
{
    if (!SPIFFS.begin(true))
    {
        DEBUG_PRINTLN("SPIFFS Mount Failed");
        return;
    }

    File file = SPIFFS.open("/pinyin.json");
    if (!file)
    {
        DEBUG_PRINTLN("Failed to open pinyin.json");
        return;
    }

    DEBUG_PRINTLN("Loading JSON pinyin dictionary...");
    size_t fileSize = file.size();
    DEBUG_PRINT("File size: ");
    DEBUG_PRINT(fileSize);
    DEBUG_PRINTLN(" bytes");

    int loadedCount = 0;
    int processedEntries = 0;

    // 使用流式解析来处理大JSON文件
    DEBUG_PRINTLN("Using streaming JSON parser...");

    // 简化的手动JSON解析（避免内存问题）
    String line;
    bool inArray = false;
    bool inObject = false;
    String currentChar = "";
    std::vector<String> currentPinyinList;

    while (file.available())
    {
        char c = file.read();

        if (c == '\n' || c == '\r')
        {
            if (line.length() > 0)
            {
                line.trim();

                // 查找字符字段
                if (line.indexOf("\"char\":") != -1)
                {
                    int startPos = line.indexOf("\"char\": \"") + 9;
                    int endPos = line.indexOf("\"", startPos);
                    if (startPos > 8 && endPos > startPos)
                    {
                        currentChar = line.substring(startPos, endPos);
                    }
                }

                // 查找拼音字段
                if (line.indexOf("\"pinyin\":") != -1)
                {
                    currentPinyinList.clear();

                    // 简单解析拼音数组
                    int arrayStart = line.indexOf("[");
                    int arrayEnd = line.indexOf("]");

                    if (arrayStart != -1 && arrayEnd != -1 && arrayEnd > arrayStart)
                    {
                        String pinyinArrayStr = line.substring(arrayStart + 1, arrayEnd);

                        // 分割拼音
                        int pos = 0;
                        while (pos < pinyinArrayStr.length())
                        {
                            int quoteStart = pinyinArrayStr.indexOf("\"", pos);
                            if (quoteStart == -1)
                                break;

                            int quoteEnd = pinyinArrayStr.indexOf("\"", quoteStart + 1);
                            if (quoteEnd == -1)
                                break;

                            String pinyin = pinyinArrayStr.substring(quoteStart + 1, quoteEnd);
                            currentPinyinList.push_back(pinyin);

                            pos = quoteEnd + 1;
                        }
                    }
                }

                // 检查是否是对象结束
                if (line.indexOf("}") != -1 && currentChar.length() > 0 && !currentPinyinList.empty())
                {
                    // 处理当前字符的所有拼音
                    for (const String &pinyin : currentPinyinList)
                    {
                        // 去除声调，转换为纯字母
                        String purePinyin = removeTones(pinyin);

                        if (purePinyin.length() > 0)
                        {
                            py2hz[purePinyin].push_back(currentChar);
                            loadedCount++;

                            // 收集调试信息但不立即输出（避免过多输出）
                        }
                    }

                    processedEntries++;

                    // 每处理500个条目输出一次进度
                    if (processedEntries % 500 == 0)
                    {
                        DEBUG_PRINT("Processed ");
                        DEBUG_PRINT(processedEntries);
                        DEBUG_PRINT(" entries, loaded ");
                        DEBUG_PRINT(loadedCount);
                        DEBUG_PRINTLN(" mappings...");
                    }

                    // 重置当前条目
                    currentChar = "";
                    currentPinyinList.clear();
                }

                line = "";
            }
        }
        else
        {
            line += c;
        }
    }

    file.close();

    DEBUG_PRINT("Processed ");
    DEBUG_PRINT(processedEntries);
    DEBUG_PRINTLN(" JSON entries.");

    DEBUG_PRINT("Loaded ");
    DEBUG_PRINT(loadedCount);
    DEBUG_PRINTLN(" pinyin mappings.");

    DEBUG_PRINT("Unique pinyin keys: ");
    DEBUG_PRINTLN(py2hz.size());

    // 输出一些示例条目供验证
    DEBUG_PRINTLN("Dictionary sample entries:");
    int sampleCount = 0;
    for (auto &pair : py2hz)
    {
        if (sampleCount >= 8)
            break;
        DEBUG_PRINT("  ");
        DEBUG_PRINT(pair.first);
        DEBUG_PRINT(" -> ");
        DEBUG_PRINT(pair.second[0]);
        if (pair.second.size() > 1)
        {
            DEBUG_PRINT(" (+" + String(pair.second.size() - 1) + " more)");
        }
        DEBUG_PRINTLN("");
        sampleCount++;
    }

    // 专门检查dong相关的条目
    DEBUG_PRINTLN("Checking 'dong' related entries:");
    for (auto &pair : py2hz)
    {
        if (pair.first.startsWith("don") || pair.first == "dong")
        {
            DEBUG_PRINT("  Found: ");
            DEBUG_PRINT(pair.first);
            DEBUG_PRINT(" -> ");
            DEBUG_PRINT(pair.second[0]);
            if (pair.second.size() > 1)
            {
                DEBUG_PRINT(" (+" + String(pair.second.size() - 1) + " more)");
            }
            DEBUG_PRINTLN("");
        }
    }
}

String removeTones(const String &pinyin)
{
    String result = "";
    String input = pinyin;

    input.replace("ā", "a");
    input.replace("á", "a");
    input.replace("ǎ", "a");
    input.replace("à", "a");
    input.replace("ē", "e");
    input.replace("é", "e");
    input.replace("ě", "e");
    input.replace("è", "e");
    input.replace("ī", "i");
    input.replace("í", "i");
    input.replace("ǐ", "i");
    input.replace("ì", "i");
    input.replace("ō", "o");
    input.replace("ó", "o");
    input.replace("ǒ", "o");
    input.replace("ò", "o");
    input.replace("ū", "u");
    input.replace("ú", "u");
    input.replace("ǔ", "u");
    input.replace("ù", "u");
    input.replace("ü", "v");
    input.replace("ǖ", "v");
    input.replace("ǘ", "v");
    input.replace("ǚ", "v");
    input.replace("ǜ", "v");
    input.replace("ń", "n");
    input.replace("ň", "n");

    input.replace("ɡ", "g");
    input.replace("ŋ", "ng");
    input.replace("ɑ", "a");
    input.replace("ɨ", "i");
    input.replace("ɯ", "u");

    input.toLowerCase();

    return input;
}

std::vector<std::vector<String>> segmentPinyin(const String &pinyin)
{
    std::vector<std::vector<String>> results;

    if (pinyin.length() == 0)
    {
        return results;
    }

    int len = pinyin.length();
    std::vector<std::vector<std::vector<String>>> dp(len + 1);
    dp[0].push_back(std::vector<String>());

    for (int i = 1; i <= len; i++)
    {
        for (int j = 0; j < i; j++)
        {
            String segment = pinyin.substring(j, i);

            if (py2hz.count(segment) > 0)
            {
                for (auto &prevSegments : dp[j])
                {
                    std::vector<String> newSegments = prevSegments;
                    newSegments.push_back(segment);
                    dp[i].push_back(newSegments);
                }
            }
        }
    }

    return dp[len];
}

std::vector<String> generateMultiCharCandidates(const std::vector<std::vector<String>> &segments)
{
    std::vector<String> results;

    for (const auto &segmentList : segments)
    {
        std::vector<std::vector<String>> charOptions;

        bool allSegmentsValid = true;
        for (const String &seg : segmentList)
        {
            if (py2hz.count(seg) > 0)
            {
                charOptions.push_back(py2hz[seg]);
            }
            else
            {
                allSegmentsValid = false;
                break;
            }
        }

        if (!allSegmentsValid)
            continue;

        std::vector<String> combinations;
        generateCombinations(charOptions, 0, "", combinations);

        for (const String &combo : combinations)
        {
            if (results.size() < 20)
            {
                results.push_back(combo);
            }
        }
    }

    return results;
}

void generateCombinations(const std::vector<std::vector<String>> &charOptions,
                          int index, const String &current,
                          std::vector<String> &results)
{
    if (index == charOptions.size())
    {
        if (results.size() < 10)
        {
            results.push_back(current);
        }
        return;
    }

    for (const String &ch : charOptions[index])
    {
        generateCombinations(charOptions, index + 1, current + ch, results);
        if (results.size() >= 10)
            break;
    }
}

void updateCandidates()
{
    candidates.clear();
    candidateIndex = 0;

    if (pinyinBuffer.length() == 0)
    {
        return;
    }

    DEBUG_PRINT("[SEARCH] Looking for: '");
    DEBUG_PRINT(pinyinBuffer);
    DEBUG_PRINTLN("'");

    std::set<String> uniqueCandidates;

    if (py2hz.count(pinyinBuffer))
    {
        for (const String &hanzi : py2hz[pinyinBuffer])
        {
            uniqueCandidates.insert(hanzi);
        }
        DEBUG_PRINT("[EXACT] Found ");
        DEBUG_PRINT(py2hz[pinyinBuffer].size());
        DEBUG_PRINTLN(" exact matches");
    }
    else
    {
        DEBUG_PRINTLN("[PREFIX] Searching for prefix matches...");
        int prefixMatches = 0;
        for (auto &pair : py2hz)
        {
            if (pair.first.startsWith(pinyinBuffer))
            {
                prefixMatches++;
                if (prefixMatches <= 3)
                {
                    DEBUG_PRINT("  Match: ");
                    DEBUG_PRINT(pair.first);
                    DEBUG_PRINT(" -> ");
                    DEBUG_PRINTLN(pair.second[0]);
                }
                for (const String &hanzi : pair.second)
                {
                    uniqueCandidates.insert(hanzi);
                }
            }
        }

        if (prefixMatches == 0 && pinyinBuffer.length() >= 4)
        {
            DEBUG_PRINTLN("[SEGMENT] Trying multi-character segmentation...");

            auto segments = segmentPinyin(pinyinBuffer);
            if (!segments.empty())
            {
                DEBUG_PRINT("[SEGMENT] Found ");
                DEBUG_PRINT(segments.size());
                DEBUG_PRINTLN(" possible segmentations");

                auto multiCandidates = generateMultiCharCandidates(segments);
                for (const String &candidate : multiCandidates)
                {
                    uniqueCandidates.insert(candidate);
                }

                DEBUG_PRINT("[MULTI] Generated ");
                DEBUG_PRINT(multiCandidates.size());
                DEBUG_PRINTLN(" multi-character candidates");
            }
        }

        DEBUG_PRINT("[TOTAL] Found ");
        DEBUG_PRINT(prefixMatches);
        DEBUG_PRINTLN(" prefix matches");
    }

    std::vector<String> rawCandidates;
    for (const String &candidate : uniqueCandidates)
    {
        rawCandidates.push_back(candidate);
        if (rawCandidates.size() >= 20)
        {
            break;
        }
    }

    candidates = sortCandidatesByFrequency(rawCandidates);

    DEBUG_PRINT("[FINAL] ");
    DEBUG_PRINT(candidates.size());
    DEBUG_PRINTLN(" candidates after frequency sorting");
}

void handlePinyinInput(char key)
{
    const char *keyChars = pinyinKeymap[key - '0'];
    int keyCharsLen = strlen(keyChars);

    if (keyCharsLen == 0)
        return;

    static char lastPinyinKey = 0;
    static unsigned long lastPinyinTime = 0;
    static int pinyinKeyIndex = 0;

    unsigned long now = millis();

    if (key == lastPinyinKey && (now - lastPinyinTime) < 800)
    {
        pinyinKeyIndex = (pinyinKeyIndex + 1) % keyCharsLen;

        if (pinyinBuffer.length() > 0)
        {
            pinyinBuffer.remove(pinyinBuffer.length() - 1);
        }
        pinyinBuffer += keyChars[pinyinKeyIndex];
    }
    else
    {
        pinyinKeyIndex = 0;
        pinyinBuffer += keyChars[pinyinKeyIndex];
    }

    lastPinyinKey = key;
    lastPinyinTime = now;

    updateCandidates();
    composing = true;

    DEBUG_PRINT("[DEBUG] Key: ");
    DEBUG_PRINT(key);
    DEBUG_PRINT(", Pinyin: ");
    DEBUG_PRINT(pinyinBuffer);
    DEBUG_PRINT(", Candidates: ");
    DEBUG_PRINT(candidates.size());
    DEBUG_PRINT(", Selected: ");
    DEBUG_PRINTLN(candidateIndex);
}

void commitCandidate()
{
    if (!candidates.empty() && candidateIndex < candidates.size())
    {
        String selectedChar = candidates[candidateIndex];
        inputBuffer += selectedChar;

        updateCharFrequency(selectedChar);

        DEBUG_PRINT("[COMMIT] Selected: ");
        DEBUG_PRINT(selectedChar);
        DEBUG_PRINT(", Input buffer: ");
        DEBUG_PRINTLN(inputBuffer);
        DEBUG_PRINT("[LEARN] Updated frequency for: ");
        DEBUG_PRINT(selectedChar);
        DEBUG_PRINT(" -> ");
        DEBUG_PRINTLN(charFrequency[selectedChar]);

        pinyinBuffer = "";
        candidates.clear();
        composing = false;
        candidateIndex = 0;
    }
}

void handleEnglishInput(char key)
{
    static char lastKey = 0;
    static unsigned long lastTime = 0;
    static int engIdx = 0;

    const char *keyChars = keymap[key - '0'];
    int keyCharsLen = strlen(keyChars);
    if (keyCharsLen == 0)
        return;

    unsigned long now = millis();

    if (key == lastKey && (now - lastTime) < 800)
    {
        engIdx = (engIdx + 1) % keyCharsLen;
        inputBuffer.remove(inputBuffer.length() - 1);
        inputBuffer += keyChars[engIdx];
    }
    else
    {
        engIdx = 0;
        inputBuffer += keyChars[engIdx];
    }

    lastKey = key;
    lastTime = now;
}

// 自动学习功能实现
void loadFrequencyData()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("[FREQ] SPIFFS not available for frequency data");
        return;
    }

    File file = SPIFFS.open(FREQ_FILE, "r");
    if (!file)
    {
        Serial.println("[FREQ] No existing frequency file, starting fresh");
        return;
    }

    int loadedEntries = 0;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.length() == 0)
            continue;

        int colonPos = line.indexOf(':');
        if (colonPos == -1)
            continue;

        String character = line.substring(0, colonPos);
        int frequency = line.substring(colonPos + 1).toInt();

        if (frequency > 0)
        {
            charFrequency[character] = frequency;
            loadedEntries++;
        }
    }

    file.close();
    Serial.print("[FREQ] Loaded ");
    Serial.print(loadedEntries);
    Serial.println(" frequency entries");
}

void saveFrequencyData()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("[FREQ] SPIFFS not available for saving");
        return;
    }

    File file = SPIFFS.open(FREQ_FILE, "w");
    if (!file)
    {
        Serial.println("[FREQ] Failed to open frequency file for writing");
        return;
    }

    int savedEntries = 0;
    for (const auto &pair : charFrequency)
    {
        if (pair.second > 0)
        {
            file.print(pair.first);
            file.print(":");
            file.println(pair.second);
            savedEntries++;
        }
    }

    file.close();
    Serial.print("[FREQ] Saved ");
    Serial.print(savedEntries);
    Serial.println(" frequency entries");
}

void updateCharFrequency(const String &character)
{
    if (character.length() == 0)
        return;

    charFrequency[character]++;

    if (charFrequency.size() > MAX_FREQ_ENTRIES)
    {
        int minFreq = INT_MAX;
        String minChar = "";

        for (const auto &pair : charFrequency)
        {
            if (pair.second < minFreq)
            {
                minFreq = pair.second;
                minChar = pair.first;
            }
        }

        if (minChar.length() > 0 && minFreq < charFrequency[character])
        {
            charFrequency.erase(minChar);
            Serial.print("[FREQ] Removed low-frequency entry: ");
            Serial.print(minChar);
            Serial.print(" (");
            Serial.print(minFreq);
            Serial.println(")");
        }
    }

    static int updateCount = 0;
    updateCount++;
    if (updateCount >= 100)
    {
        saveFrequencyData();
        updateCount = 0;
    }
}

std::vector<String> sortCandidatesByFrequency(const std::vector<String> &rawCandidates)
{
    if (rawCandidates.empty())
        return rawCandidates;

    std::vector<std::pair<String, int>> candidatesWithFreq;

    for (const String &candidate : rawCandidates)
    {
        int freq = 0;
        if (charFrequency.count(candidate) > 0)
        {
            freq = charFrequency[candidate];
        }
        candidatesWithFreq.push_back(std::make_pair(candidate, freq));
    }

    std::sort(candidatesWithFreq.begin(), candidatesWithFreq.end(),
              [](const std::pair<String, int> &a, const std::pair<String, int> &b)
              {
                  if (a.second != b.second)
                  {
                      return a.second > b.second;
                  }
                  return a.first < b.first;
              });

    std::vector<String> sortedCandidates;
    for (const auto &pair : candidatesWithFreq)
    {
        sortedCandidates.push_back(pair.first);
    }

    if (sortedCandidates.size() > 0)
    {
        Serial.print("[SORT] First 3 candidates by frequency: ");
        for (int i = 0; i < min(3, (int)sortedCandidates.size()); i++)
        {
            Serial.print(sortedCandidates[i]);
            Serial.print("(");
            Serial.print(charFrequency.count(sortedCandidates[i]) > 0 ? charFrequency[sortedCandidates[i]] : 0);
            Serial.print(") ");
        }
        Serial.println();
    }

    return sortedCandidates;
}
