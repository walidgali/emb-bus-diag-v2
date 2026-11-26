[![C Build and Test](https://github.com/walidgali/emb-bus-diag-v2/actions/workflows/ci.yml/badge.svg)](https://github.com/walidgali/emb-bus-diag-v2/actions/workflows/ci.yml)

*English: [README.md](README.md)*

# Emb-Bus-Diag V2

أداة سطر أوامر معيارية بلغة C11 لتشخيص الأنظمة المدمجة. تضم طبقة تجريد للعتاد (I2C وSPI وCAN وUART وModbus TCP والوضع التجريبي Mock)، ومحرك إحصاءات لحظي (المتوسط المتحرك الأسّي EMA والتباين وكشف القيم الشاذة بدرجة Z والميل بالمربعات الصغرى)، وتقارير JSON. ومحرك الإحصاءات متاح أيضاً كمكتبة مشتركة لـ Python (ctypes).

## الوثائق

| الملف | المحتوى |
|-------|---------|
| [docs/GUIDE.ar.md](docs/GUIDE.ar.md) | الدليل الكامل بالعربية من الصفر (ومعه [نسخة Word](docs/GUIDE.ar.docx)) |
| [docs/GUIDE.en.md](docs/GUIDE.en.md) | الدليل الكامل بالإنجليزية ([نسخة Word](docs/GUIDE.en.docx)) |
| [docs/HARDWARE.ar.md](docs/HARDWARE.ar.md) | التوصيلات وخيارات كل ناقل وبروتوكول UART (بالعربية) |
| [docs/HARDWARE.md](docs/HARDWARE.md) | الدليل نفسه بالإنجليزية |

## البناء والتشغيل بسرعة

```bash
sudo apt install build-essential libmodbus-dev pkg-config   # libmodbus اختيارية
make            # بناء الأداة (يُكتشف Modbus تلقائياً؛ WITH_MODBUS=0 أو 1 للتحكم)
make lib        # المكتبة libembdiag.so لـ Python
make check      # اختبارات C وCLI وPython
make SAN=1 test # الاختبارات نفسها مع ASan وUBSan

./emb-bus-diag-v2 --bus mock -n 10 -i 100
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219
EMBDIAG_UART_DEV=/dev/ttyUSB0 ./emb-bus-diag-v2 --bus uart -r 2
./emb-bus-diag-v2 --help
```

- **الملفات الجاهزة:** `raw` و`ina219` و`ads1115` و`tmp102`. يمكن تجاوزها بالخيارين `--metric voltage|temp|pressure` و`--scale <x>`.
- **التقرير:** يُكتب في `report.json` (غيّره بالخيار `-o`).
- **رموز الخروج:** `0` نجاح، `1` خطأ في الخيارات أو الفتح أو القراءة، `2` قراءة بحالة CRITICAL أو ERROR.

## ملاحظات إحصائية

- **درجة Z** تقارن كل عينة جديدة بالتاريخ السابق لها، فيُكشف الارتفاع المفاجئ حتى في السلاسل القصيرة (تظهر بعد توفر 4 عينات سابقة). القيمة المطلقة الأكبر من 3 تُعلَّم `OUTLIER`. وإن كان التاريخ بلا تباين فأي انحراف يعطي `±99`.
- **الاتجاه (Trend)** هو ميل المربعات الصغرى مقابل رقم العينة.
- **المدى (Range)** هو `max - min`.
