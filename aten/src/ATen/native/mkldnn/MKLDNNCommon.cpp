#include <ATen/native/mkldnn/MKLDNNCommon.h>
#include <ATen/OpaqueTensorImpl.h>
#include <c10/core/Allocator.h>

#if AT_MKLDNN_ENABLED()

#include <ideep.hpp>

namespace at { namespace native {

/**
 * `IntrusivePtrTargetWrapper` wraps a custom storage handle  of a tensor
*  (as template param) and inherits `c10::intrusive_ptr_target` so that it
*  can be used with `c10::intrusive_ptr`.
 *
 * It currently only supports wrapping the custom handle by:
 * - Constructing with an existing custom handle by copy/move constructor.
 *
 * See `OpaqueTensorImpl::opaque_handle_`.
 *
 * NOTE: if this is generally useful we may want to move this to its own header.
 */
template <typename T>
struct CAFFE2_API IntrusivePtrTargetWrapper : c10::intrusive_ptr_target {
private:
  T target_;

public:
  IntrusivePtrTargetWrapper() = delete;
  IntrusivePtrTargetWrapper(const T& target): target_(target) {}
  IntrusivePtrTargetWrapper(T&& target): target_(std::move(target)) {}

  bool is_contiguous(c10::MemoryFormat memory_format) {
    AT_ASSERTM(
	       memory_format == c10::MemoryFormat::Contiguous,
	       "is_contiguous expects Contiguous memory format.");
    return true;
  }

  T& get_target() {
    return target_;
  }

  void* get_raw_data_ptr() {
    if (target_.get_data_handle() != nullptr) {
      return static_cast<void*>(target_.get_data_handle());
    }
    return nullptr;
  }
};

using IDeepTensorWrapper = IntrusivePtrTargetWrapper<ideep::tensor>;
using IDeepTensorWrapperPtr = c10::intrusive_ptr<IDeepTensorWrapper>;
using MKLDNNTensorImpl = OpaqueTensorImpl<IDeepTensorWrapperPtr>;
using MKLDNNTensor = Tensor;

Tensor new_with_itensor_mkldnn(ideep::tensor&& it, const TensorOptions& options) {
  // NOTE: int32_t dims from ideep::tensor but sizes needs int64_t
  // TODO: support int64_t dims in ideep::tensor to avoid extra conversion
  auto dims = it.get_dims();
  IDeepTensorWrapperPtr handle = c10::make_intrusive<IDeepTensorWrapper>(std::move(it));
  return detail::make_tensor<MKLDNNTensorImpl>(
    DispatchKeySet(DispatchKey::MkldnnCPUTensorId),
    options.dtype(), options.device(), handle,
    std::vector<int64_t>(dims.begin(), dims.end()));
}

ideep::tensor& itensor_from_mkldnn(const MKLDNNTensor& mkldnn_tensor) {
  AT_ASSERTM(mkldnn_tensor.is_mkldnn(),
             "mkldnn_to_dense expects MKL-DNN tensor input");
  TORCH_INTERNAL_ASSERT(at::impl::variable_excluded_from_dispatch());
  MKLDNNTensorImpl *mklimpl = static_cast<MKLDNNTensorImpl *>(mkldnn_tensor.unsafeGetTensorImpl());
  return mklimpl->unsafe_opaque_handle()->get_target();
}

ideep::tensor itensor_view_from_dense(const Tensor& tensor) {
  AT_ASSERTM(
      tensor.device().type() == DeviceType::CPU,
      "itensor_view_from_dense expects CPU tensor input");
  AT_ASSERTM(
      tensor.layout() == Layout::Strided,
      "itensor_view_from_dense expects dense tensor input");
  AT_ASSERTM(tensor.scalar_type() == ScalarType::Float,
             "itensor_view_from_dense expects float tensor input");
  TORCH_INTERNAL_ASSERT(at::impl::variable_excluded_from_dispatch());
  return {{{tensor.sizes().cbegin(), tensor.sizes().cend()},
           ideep::tensor::data_type::f32},
          tensor.template data_ptr<float>()};
}

// Note in case the aten Tensor is a dense tensor, the retured ideep
// tensor is just a view of the storage of the aten dense tensor, so
// caller needs to make sure the aten dense tensor's lifetime is
// longer than the ideep tensor.
ideep::tensor itensor_from_tensor(const at::Tensor& tensor) {
  if (tensor.is_mkldnn()) {
    return at::native::itensor_from_mkldnn(tensor);
  } else {
    return at::native::itensor_view_from_dense(tensor);
  }
}

}}

#endif // AT_MKLDNN_ENABLED()
