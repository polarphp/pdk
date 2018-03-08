// @copyright 2017-2018 zzu_softboy <zzu_softboy@163.com>
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Created by softboy on 2018/03/08.

#ifndef PDK_KERNEL_INTERNAL_OBJECT_DEFS_PRIVATE_H
#define PDK_KERNEL_INTERNAL_OBJECT_DEFS_PRIVATE_H

#include "pdk/base/os/thread/Atomic.h"

namespace pdk {
namespace kernel {

// forward declare class
class Object;

namespace internal {

using pdk::os::thread::AtomicInt;
using pdk::kernel::Object;

class SlotObjectBase
{
   AtomicInt m_ref;
   // don't use virtual functions here; we don't want the
   // compiler to create tons of per-polymorphic-class stuff that
   // we'll never need. We just use one function pointer.
   typedef void (*ImplFunc)(int which, SlotObjectBase* this_, Object *receiver, void **args, bool *ret);
   const ImplFunc m_impl;
protected:
   enum class Operation {
      Destroy,
      Call,
      Compare,
      NumOperations
   };
public:
   explicit SlotObjectBase(ImplFunc func)
      : m_ref(1),
        m_impl(func)
   {}
   
   inline int ref() noexcept
   {
      return m_ref.ref();
   }
   
   inline void destroyIfLastRef() noexcept
   {
      if (!m_ref.deref()) {
         m_impl(Operation::Destroy, this, nullptr, nullptr, nullptr);
      }
   }
   
   inline bool compare(void **a)
   {
      bool ret = false;
      m_impl(Operation::Compare, this, nullptr, a, &ret);
      return ret;
   }
   
   inline void call(Object *receiver, void **args)
   {
      m_impl(Operation::Call, this, receiver, args, nullptr);
   }
protected:
   ~SlotObjectBase()
   {}
   
private:
   PDK_DISABLE_COPY(SlotObjectBase);
};

// implementation of SlotObjectBase for which the slot is a pointer to member function of a QObject
// Args and R are the List of arguments and the returntype of the signal to which the slot is connected.
template<typename Func, typename Args, typename R>
class SlotObject : public SlotObjectBase
{
   typedef QtPrivate::FunctionPointer<Func> FuncType;
   Func function;
   static void impl(int which, SlotObjectBase *this_, QObject *r, void **a, bool *ret)
   {
      switch (which) {
      case Destroy:
         delete static_cast<SlotObject*>(this_);
         break;
      case Call:
         FuncType::template call<Args, R>(static_cast<SlotObject*>(this_)->function, static_cast<typename FuncType::Object *>(r), a);
         break;
      case Compare:
         *ret = *reinterpret_cast<Func *>(a) == static_cast<SlotObject*>(this_)->function;
         break;
      case NumOperations: ;
      }
   }
public:
   explicit SlotObject(Func f) : SlotObjectBase(&impl), function(f) {}
};

} // internal
} // kernel
} // pdk

#endif // PDK_KERNEL_INTERNAL_OBJECT_DEFS_PRIVATE_H
