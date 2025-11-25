#!/usr/bin/env python3
"""
ESP32 无线IME 加密通信测试脚本
用于验证和测试加密解密功能
"""


def xor_encrypt_decrypt(text, key):
    """XOR加密/解密函数"""
    result = ""
    key_len = len(key)

    for i, char in enumerate(text):
        encrypted_byte = ord(char) ^ ord(key[i % key_len])
        result += f"{encrypted_byte:02X}"

    return result


def hex_to_text(hex_string, key):
    """将十六进制字符串解密为文本"""
    if len(hex_string) % 2 != 0:
        return "错误：十六进制长度不是偶数"

    result = ""
    key_len = len(key)

    for i in range(0, len(hex_string), 2):
        hex_byte = hex_string[i : i + 2]
        encrypted_byte = int(hex_byte, 16)
        decrypted_byte = encrypted_byte ^ ord(key[(i // 2) % key_len])
        result += chr(decrypted_byte)

    return result


def crc16_calculate(data):
    """计算CRC16校验和"""
    crc = 0
    for char in data:
        crc ^= ord(char)
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc = crc >> 1

    return crc


def parse_packet(packet, key):
    """解析安全数据包"""
    print(f"原始数据包: {packet}")
    print(f"数据包长度: {len(packet)}")

    # 检查最小长度
    if len(packet) < 26:
        return "错误：数据包太短"

    # 解析各部分
    header = packet[0:3]
    version = packet[3:4]
    device_id = packet[4:20]
    data_and_checksum = packet[20:]

    print(f"包头: {header}")
    print(f"版本: {version}")
    print(f"设备ID: {device_id}")

    if header != "WIM":
        return "错误：包头不正确"

    # 分离数据和校验和
    checksum = data_and_checksum[-4:]
    encrypted_data = data_and_checksum[:-4]

    print(f"加密数据: {encrypted_data}")
    print(f"校验和: {checksum}")

    # 验证校验和
    packet_without_checksum = packet[:-4]
    calculated_crc = crc16_calculate(packet_without_checksum)
    calculated_checksum = f"{calculated_crc:04X}"

    print(f"计算的校验和: {calculated_checksum}")

    if checksum.upper() != calculated_checksum.upper():
        return "错误：校验和验证失败"

    # 解密数据
    decrypted = hex_to_text(encrypted_data, key)

    return {"设备ID": device_id, "解密消息": decrypted, "校验结果": "通过"}


def main():
    print("=== ESP32 无线IME 加密通信测试工具 ===\n")

    # 默认密钥（与ESP32代码中相同）
    default_key = "WirelessIME2024Key!"
    print(f"使用密钥: {default_key}\n")

    while True:
        print("\n选择操作:")
        print("1. 加密文本")
        print("2. 解密十六进制")
        print("3. 解析完整数据包")
        print("4. 测试示例")
        print("0. 退出")

        choice = input("\n请输入选择 (0-4): ").strip()

        if choice == "0":
            print("再见！")
            break

        elif choice == "1":
            text = input("输入要加密的文本: ")
            encrypted = xor_encrypt_decrypt(text, default_key)
            print(f"加密结果: {encrypted}")

        elif choice == "2":
            hex_string = input("输入十六进制字符串: ").strip().replace(" ", "")
            try:
                decrypted = hex_to_text(hex_string, default_key)
                print(f"解密结果: {decrypted}")
            except Exception as e:
                print(f"解密失败: {e}")

        elif choice == "3":
            packet = input("输入完整数据包: ").strip().replace(" ", "")
            try:
                result = parse_packet(packet, default_key)
                if isinstance(result, dict):
                    print("\n解析结果:")
                    for key, value in result.items():
                        print(f"  {key}: {value}")
                else:
                    print(f"解析失败: {result}")
            except Exception as e:
                print(f"解析失败: {e}")

        elif choice == "4":
            print("\n=== 测试示例 ===")

            # 测试加密
            test_message = "Hello World!"
            print(f"原文: {test_message}")

            encrypted = xor_encrypt_decrypt(test_message, default_key)
            print(f"加密: {encrypted}")

            # 测试解密
            decrypted = hex_to_text(encrypted, default_key)
            print(f"解密: {decrypted}")
            print(f"验证: {'通过' if test_message == decrypted else '失败'}")

            # 创建完整数据包示例
            header = "WIM"
            version = "1"
            device_id = "IME_345F45AACBCC"
            packet_base = header + version + device_id + encrypted

            # 计算校验和
            crc = crc16_calculate(packet_base)
            checksum = f"{crc:04X}"

            full_packet = packet_base + checksum
            print(f"\n完整数据包: {full_packet}")

            # 解析测试
            parse_result = parse_packet(full_packet, default_key)
            if isinstance(parse_result, dict):
                print("\n数据包解析测试: 通过")
            else:
                print(f"数据包解析测试: 失败 - {parse_result}")

        else:
            print("无效选择，请重试")


if __name__ == "__main__":
    main()
