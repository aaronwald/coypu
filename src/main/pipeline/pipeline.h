#ifndef __COYPU_PIPELINE_H
#define __COYPU_PIPELINE_H

namespace coypu {
    namespace pipeline {

    template <typename T, typename... Tail> class PipelineHandler;

    template <typename T, template <typename> class Head, typename... Tail>
    class PipelineHandler<T, Head<T>, Tail...>
    {
    	public:
			T &operator()(T &t)
			{
				return _ph(_head(t));
			}

    	private:
			PipelineHandler<T, Tail...> _ph;
			Head<T> _head;
    };

    template <typename T>
    class PipelineHandler<T>
    {
    	public:
			T &operator()(T &t)
			{
				return t;
			}
    };
    } // namespace pipeline
} // namespace coypu

#endif