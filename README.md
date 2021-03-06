http://habrahabr.ru/company/pt/blog/255487/




Сначала несколько второстепенных заметок относительно дизайна.

Класс не поддерживает семантику копирования, но поддерживает семантику перемещения, таким образом он реализует модель единоличного владения (как std::unique_ptr). При необходимости можно определить аналогичный класс, реализующий модель совместного владения (как std::shared_ptr).
Принимая во внимание тот факт, что большинство аргументов ResourceType на практике являются примитивными дескрипторами (например, void* или int), методы класса помечены, как noexcept.
Перегрузка operator& – спорное решение. Так или иначе, я решил сделать это, чтобы облегчить использование класса с функциями-фабриками вида CreateHandle(Handle* handle). Разумной альтернативой в данном случае является обычная именованная функция-член.

Теперь к делу. Как мы видим, метод Cleanup, являющийся краеугольным камнем нашего класса, оставлен без определения. В результате попытка создания объекта класса неминуемо приведёт к ошибке. Трюк заключается в том, что мы должны определить явную специализацию метода Сleanup для каждого ресурса, которым хотим управлять. Например:

```
// Here "FileId" is some OS-specific file descriptor type
// which must be closed with CloseFile function.
using File = Resource<struct FileIdTag, FileId>;
template<> void File::Cleanup() noexcept {
  if (resource_)
    CloseFile(resource_);
}
```
Теперь мы можем использовать наш класс для управления объектами FileId:
```
{
  File file{ CreateFile(file_path) };
  ...
} // "file" will be destroyed here
```
Мы можем рассматривать объявление Cleanup внутри Resource как своего рода «чисто виртуальную функцию времени компиляции». Схожим образом, явная специализация Cleanup для FileId является «конкретной реализацией» этой функции.

Что за параметр такой — ResourceTag?

Кто-то может поинтересоваться, зачем нам нужен неиспользуемый параметр шаблона ResourceTag? Он служит двум целям.

Во-первых, типобезопасность. Представим, что два разных типа ресурсов определены как синонимы для типа void*. Без параметра-тега компилятор просто не сможет обнаружить баг в следующем коде:
```
using ScopedBitmap = Resource<Bitmap>;
using ScopedTexture = Resource<Texture>;
void DrawBitmap(DeviceContext& ctx, ScopedBitmap& bmp) {
  /* ... */
}

int main() {
  DeviceContext ctx;
  ScopedBitmap bmp;
  ScopedTexture t;
  // Passing texture to function expecting bitmap.
  // Compiles OK.
  DrawBitmap(ctx, t);
}
```
Если же мы используем тег, компилятор заметит ошибку:
```
using ScopedBitmap = Resource<struct BitmapTag, Bitmap>;
using ScopedTexture = Resource<struct TextureTag, Texture>;

int main() {
  DeviceContext ctx;
  ScopedBitmap bmp;
  ScopedTexture t;
  DrawBitmap(ctx, t);  // error: type mismatch
}
```
Второе назначение тега следующее: он позволяет нам определять специализации Cleanup для концептуально разных ресурсов, имеющих один и тот же C++ тип. Ещё раз, представим, что ресурс Bitmap удаляется с помощью функции DestroyBitmap, в то время как ресурс Texture – DestroyTexture. Не используй мы тег, ScopedBitmap и ScopedTexture имели бы одинаковый тип (напомню, в нашем примере и Bitmap, и Texture определены как void*), что не позволило бы нам определить разные функции очистки для каждого из ресурсов.

Коль уж речь у нас зашла о теге, следующее выражение может показаться странным:
```
using File = Resource<struct FileIdTag, FileId>; 
```
В частности, я говорю об использовании конструкции struct FileIdTag в качестве аргумента шаблона. Давайте рассмотрим эквивалентное выражение, смысл которого я думаю ясен тем, кто знаком с диспетчированием на основе тегов:
```
struct FileIdTag{};
using File = Resource<FileIdTag, FileId>;
```
Традиционное диспетчирование с помощью тегов подразумевает использование перегруженных функций, в которых аргумент с типом тега является селектором перегрузки. Тег передаётся в перегруженную функцию по значению, поэтому он должен быть полным типом. Однако, в нашем случае перегрузка функций не используется. Тег нужен нам лишь как аргумент шаблона, чтобы обеспечить возможность определения явных специализаций для различных ресурсов. Принимая во внимания тот факт, что C++ позволяет использовать неполный тип в качестве аргумента шаблона, мы можем заменить определение тега на его объявление:
```
struct FileIdTag;
using File = Resource<FileIdTag, FileId>;
```
Далее, учитывая, что FileIdTag используется только внутри объявления синонима типа, мы можем перенести его непосредственно в место использования:
```
using File = Resource<struct FileIdTag, FileId>; 
```
Делаем требование явной специализации более… явным

Если пользователь не предоставит явную специализацию для метода Cleanup, он/она не сможет собрать программу. Это намеренное поведение. Однако, с ним связана пара проблем:

ошибка выбрасывается во время компоновки, в то время как предпочтительно (и возможно) обнаружить её раньше, на этапе компиляции;
сообщение об ошибке не даёт пользователю подсказки относительно истинной причины проблемы и пути её решения.
Давайте попробуем исправить эти недостатки с помощью static_assert:
```
void Cleanup() noexcept {
  static_assert(false,
                "This function must be explicitly specialized.");
}
```
К сожалению, нет гарантии, что это сработает: утверждение может выбросить ошибку даже если основной шаблон Cleanup никогда не будет инстанцирован. Причина в следующем: условие внутри static_assert никак не зависит от параметров шаблона класса, таким образом, компилятор имеет право вычислить условие ещё до того, как попытается инстанцировать шаблон.

Зная это, проблему легко решить: сделать условие зависимым от параметров шаблона. В частности, мы можем определить функцию-член времени компиляции, которая всегда возвращает значение false:
```
static constexpr bool False() noexcept { return false; }

void Cleanup() noexcept {
  static_assert(False(),
                "This function must be explicitly specialized.");
}
```
Тонкие обёртки против высокоуровневых абстракций

Шаблон RAII-обёртки, представленный в статье, является тонкой абстракцией, имеющей дело исключительно с управлением ресурсами. Кто-то может возразить, зачем вообще писать такой класс, не стоит ли сразу реализовать полноценную абстракцию в лучших традициях объектно‑ориентированного проектирования? В качестве примера, посмотрим, как мы могли бы написать класс битовой карты с нуля:
```
class Bitmap {
public:
  Bitmap(int width, int height);
  ~Bitmap();
  
  int Width() const;
  int Height() const;
  
  Colour PixelColour(int x, int y) const;
  void PixelColour(int x, int y, Colour colour);
  
  DC DeviceContext() const;
  
  /* Other methods... */

private:
  int width_{};
  int height_{};
  
  // Raw resources.
  BITMAP bitmap_{};
  DC device_context_{};
};
```
Чтобы понять, почему такой подход является в общем случае плохой идеей, давайте попробуем написать конструктор для класса Bitmap:
```
Bitmap::Bitmap(int width, int height)
  : width_{ width }, height_{ height } {

  // Create bitmap.
  bitmap_ = CreateBitmap(width, height);
  if (!bitmap_)
    throw std::runtime_error{ "Failed to create bitmap." };

  // Create device context.
  device_context_ = CreateCompatibleDc();
  if (!device_context_)
    // bitmap_ will be leaked here!
    throw std::runtime_error{ "Failed to create bitmap DC." };

  // Select bitmap into device context.
  // ...
}
```
Как мы видим, наш класс на самом деле управляет двумя ресурсами: непосредственно битовой картой и соответствующим контекстом устройства (этот пример вдохновлён Windows GDI, где битовой карте, как правило, соответствует контекст устройства в памяти, необходимый для операций отрисовки и интероперабельности с современными графическими интерфейсами программирования). И вот здесь то и возникает проблема: если инициализация device_context_ завершится ошибкой, произойдёт утечка bitmap_!

Теперь рассмотрим аналогичный код с использованием управляемых ресурсов:
```
using ScopedBitmap = Resource<struct BitmapTag, BITMAP>;
using ScopedDc = Resource<struct DcTag, DC>;

...

Bitmap::Bitmap(int width, int height) 
  : width_{ width }, height_{ height } {

  // Create bitmap.
  bitmap_ = ScopedBitmap{ CreateBitmap(width, height) };
  if (!bitmap_)
    throw std::runtime_error{ "Failed to create bitmap." };

  // Create device context.
  device_context_ = ScopedDc{ CreateCompatibleDc() };
  if (!device_context_)
    // Safe: bitmap_ will be destroyed in case of
    // exception.  
    throw std::runtime_error{ "Failed to create bitmap DC." };

  // Select bitmap into device context.
  // ...
}
```
Этот пример позволяет нам сформулировать следующее правило: не храните в качестве членов данных класса более одного неуправляемого ресурса. Лучше примните RAII к каждому из ресурсов, и затем используйте их как строительные блоки для построения более высокоуровневых абстракций. Такой подход обеспечивает как безопасность исключений, так и повторное использование кода (вы можете рекомбинировать эти строительные блоки в будущем без боязни взывать утечки памяти).

Ещё примеры

Ниже приведены реальные примеры полезных специализаций нашего класса для объектов Windows API. Я выбрал Windows API, так как он изобилует возможностями для применения RAII (примеры интуитивно понятны; знание Windows API не требуется).
```
// Windows handle.
using Handle = Resource<struct HandleTag, HANDLE>;
template<> void Handle::Cleanup() noexcept {
  if (resource_ && resource_ != INVALID_HANDLE_VALUE)
    CloseHandle(resource_);
}

// WinInet handle.
using InetHandle = Resource<struct InetHandleTag, HINTERNET>;
template<> void InetHandle::Cleanup() noexcept {
  if (resource_)
    InternetCloseHandle(resource_);
}

// WinHttp handle.
using HttpHandle = Resource<struct HttpHandleTag, HINTERNET>;
template<> void HttpHandle::Cleanup() noexcept {
  if (resource_)
    WinHttpCloseHandle(resource_);
}

// Pointer to SID.
using Psid = Resource<struct PsidTag, PSID>;
template<> void Psid::Cleanup() noexcept {
  if (resource_)
    FreeSid(resource_);
}

// Network Management API string buffer.
using NetApiString = Resource<struct NetApiStringTag, wchar_t*>;
template<> void NetApiString::Cleanup() noexcept {
  if (resource_ && NetApiBufferFree(resource_) != NERR_Success) {
    // Log diagnostic message in case of error.
  }
}

// Certificate store handle.
using CertStore = Resource<struct CertStoreTag, HCERTSTORE>;
template<> void CertStore::Cleanup() noexcept {
  if (resource_)
    CertCloseStore(resource_, CERT_CLOSE_STORE_FORCE_FLAG);
}
```
О чём нужно помнить, определяя явные специализации шаблонов:

явная специализация должна быть определена в том же пространстве имен, что и основной шаблон (в нашем случае, шаблон класса Resource);
явная специализация шаблона функции, определённая в заголовочном файле, должна быть встроенной (inline): запомните, явная специализация – это уже не шаблон, а обычная функция.
