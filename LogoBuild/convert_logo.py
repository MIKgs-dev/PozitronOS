#!/usr/bin/env python3
r"""
ПРОСТОЙ КОНВЕРТЕР ЛОГОТИПА для PozitronOS
Просто укажи файл - получишь массив!

Для начала стоит открыть cmd и там ввести путь. На пример: cd C:\Users\Admin\Desktop\
Далее ввести команду: C:\Users\Admin\Desktop>python convert_logo.py pozitron-logo.jpg
Где python convert_logo.py - указания питону исполняемого файла, а pozitron-logo.jpg - название изображения
"""

import sys
import os

# Проверяем зависимости
try:
    from PIL import Image
    import numpy as np
except ImportError:
    print("УСТАНОВИ СНАЧАЛА: pip install pillow numpy")
    print("Или: pip3 install pillow numpy")
    sys.exit(1)

def convert_logo(input_file):
    """
    Конвертирует JPG в чистый массив с правильным порядком ресайза
    """
    print(f"🔄 Конвертирую: {input_file}")
    
    # Открываем изображение
    img = Image.open(input_file)
    
    # Делаем белый фон
    if img.mode == 'RGBA':
        bg = Image.new('RGB', img.size, (255, 255, 255))
        bg.paste(img, (0, 0), img)
        img = bg
    elif img.mode != 'RGB':
        img = img.convert('RGB')
    
    # Шаг 1: Сначала переводим в полутона (градации серого 0-255)
    gray = img.convert('L')
    
    # Шаг 2: Масштабируем до 256x256 ПОЛУТОНОВУЮ картинку.
    # Теперь Lanczos сделает края букв идеально субпиксельно-плавными!
    gray_resized = gray.resize((256, 256), Image.Resampling.LANCZOS)
    
    # Шаг 3: И только теперь делаем ОДИН ОДИНЕШЕНЕК порог бинаризации.
    # Так как исходник у тебя — четкий фиолетовый на черном (или наоборот), 
    # порог в районе 128 срежет плавный край ровно посередине, создав идеальный векторный силуэт.
    binary = gray_resized.point(lambda x: 0 if x < 129 else 255, '1')
    
    # Сохраняем для проверки
    binary.save("logo_result.png")
    print("✅ Превью сохранено: logo_result.png")
    
    # Конвертируем в массив
    pixels = np.array(binary)
    
    bytes_array = []
    for y in range(256):
        for x in range(0, 256, 8):
            byte = 0
            for bit in range(8):
                if pixels[y, x + bit] == 0:  # Черный пиксель
                    byte |= (1 << (7 - bit))
            bytes_array.append(byte)
    
    print("\n" + "="*60)
    print("СКОПИРУЙ ЭТОТ МАССИВ В logo.c:")
    print("="*60)
    print(f"const uint8_t logo_bitmap[{len(bytes_array)}] = {{")
    
    for i in range(0, len(bytes_array), 16):
        line = bytes_array[i:i+16]
        hex_line = ', '.join(f'0x{b:02x}' for b in line)
        if i + 16 < len(bytes_array):
            print(f"    {hex_line},")
        else:
            print(f"    {hex_line}")
    
    print("};")
    
    print("\n" + "="*60)
    print("ОБНОВИ logo.h ТАК:")
    print("="*60)
    print("#define LOGO_WIDTH 256")
    print("#define LOGO_HEIGHT 256")
    print("#define LOGO_COLOR 0x3F47CC")
    
    print("\n✅ ГОТОВО! Проверь logo_result.png")

# Запуск
if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Использование: python convert_logo_simple.py ТВОЙ_ЛОГО.jpg")
        print("Пример: python convert_logo_simple.py pozitron-logo.jpg")
        sys.exit(1)
    
    if not os.path.exists(sys.argv[1]):
        print(f"❌ Файл не найден: {sys.argv[1]}")
        sys.exit(1)
    
    convert_logo(sys.argv[1])