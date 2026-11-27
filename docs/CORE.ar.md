# النواة المحمولة (`embdiag-core`)

*English: [CORE.md](CORE.md)*

فك ترميز الحساسات والعتبات والإحصاءات اللحظية موجودة في مكتبة صغيرة لا تعتمد على Linux،
ليعمل الكود نفسه لاحقاً على Zephyr أو FreeRTOS أو بدون نظام تشغيل.

| الملف | الدور |
|-------|-------|
| `include/embdiag_core.h` | الواجهة العامة والأنواع |
| `core/embdiag_core.c` | التنفيذ (الملف الوحيد الذي تحتاج نقله) |
| `src/diag.c` | إضافات Linux وسطر الأوامر: تحليل النصوص وتصدير JSON (تحتاج libc) |

## الضمانات (يتحقق منها CI)

- لا تخصيص ذاكرة ديناميكي ولا stdio ولا استدعاءات نظام أو ملفات. المستدعي يملك كل الذاكرة.
- كل دالة تنتهي في زمن محدود ولا تحجب التنفيذ.
- تُترجم بمعيار C99 الصارم (`-std=c99 -Wpedantic -ffreestanding`).
- مع `-DEMBDIAG_NO_LIBM` لا يحتوي الملف الناتج **أي رموز غير معرّفة** إطلاقاً (يفشل `make core-check` خلاف ذلك). وبدون الخيار تُستعمل `sqrt` من libm.
- غير آمنة للخيوط على السياق الواحد: استخدم `diag_stats_t` لكل حساس، وخيطاً واحداً (أو قفلاً) لكل سياق.

## الاستخدام

```c
#include "embdiag_core.h"

diag_stats_t st;
stats_init(&st);

diag_reading_t r;
int32_t counts = apply_sensor_profile_raw(raw16, PROFILE_INA219);
evaluate_signal(counts, 0.004, METRIC_VOLTAGE, &r);   /* r.status, r.scaled_val */
stats_update(&st, r.scaled_val);                      /* st.ema_val, st.z_score ... */
```

ضمّ `core/embdiag_core.c` إلى برنامجك الثابت مع `-Iinclude`، وأضف `-DEMBDIAG_NO_LIBM` إن لم تكن في سلسلة الأدوات مكتبة رياضيات.

## الحدود (كن صريحاً مع عملائك)

- لم تُشغَّل بعد على RTOS حقيقي أو متحكم دقيق: اختُبرت على Linux فقط. ولا توجد وحدة Zephyr ولا منفذ FreeRTOS بعد.
- كل الحسابات بنوع `double`، وهذا بطيء على متحكم بدون وحدة FPU. ونسخة الأعداد الصحيحة (fixed-point) غير منفّذة.
- `diag_sqrt` بدون libm دقيقتها نحو 1e-9 (نسبياً) للمدخلات بين 1e-12 و1e24، وهذا مُختبَر.
