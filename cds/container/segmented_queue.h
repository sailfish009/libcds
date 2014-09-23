//$$CDS-header$$

#ifndef __CDS_CONTAINER_SEGMENTED_QUEUE_H
#define __CDS_CONTAINER_SEGMENTED_QUEUE_H

#include <memory>
#include <cds/intrusive/segmented_queue.h>
#include <cds/details/trivial_assign.h>
#include <cds/ref.h>

namespace cds { namespace container {

    /// SegmentedQueue -related declarations
    namespace segmented_queue {

#   ifdef CDS_DOXYGEN_INVOKED
        /// SegmentedQueue internal statistics
        typedef cds::intrusive::segmented_queue::stat stat;
#   else
        using cds::intrusive::segmented_queue::stat;
#   endif

        /// SegmentedQueue empty internal statistics (no overhead)
        typedef cds::intrusive::segmented_queue::empty_stat empty_stat;

        /// SegmentedQueue default type traits
        struct type_traits {

            /// Item allocator. Default is \ref CDS_DEFAULT_ALLOCATOR
            typedef CDS_DEFAULT_ALLOCATOR   node_allocator;

            /// Item counter, default is atomicity::item_counter
            /**
                The item counting is an essential part of segmented queue algorithm.
                The \p empty() member function is based on checking <tt>size() == 0</tt>.
                Therefore, dummy item counter like atomicity::empty_item_counter is not the proper counter.
            */
            typedef atomicity::item_counter item_counter;

            /// Internal statistics, possible predefined types are \ref stat, \ref empty_stat (the default)
            typedef segmented_queue::empty_stat        stat;

            /// Memory model, default is opt::v::relaxed_ordering. See cds::opt::memory_model for the full list of possible types
            typedef opt::v::relaxed_ordering  memory_model;

            /// Alignment of critical data, default is cache line alignment. See cds::opt::alignment option specification
            enum { alignment = opt::cache_line_alignment };

            /// Segment allocator. Default is \ref CDS_DEFAULT_ALLOCATOR
            typedef CDS_DEFAULT_ALLOCATOR allocator;

            /// Lock type used to maintain an internal list of allocated segments
            typedef cds::lock::Spin lock_type;

            /// Random \ref cds::opt::permutation_generator "permutation generator" for sequence [0, quasi_factor)
            typedef cds::opt::v::random2_permutation<int>    permutation_generator;
        };

         /// Metafunction converting option list to traits for SegmentedQueue
        /**
            The metafunction can be useful if a few fields in \ref type_traits should be changed.
            For example:
            \code
            typedef cds::container::segmented_queue::make_traits<
                cds::opt::item_counter< cds::atomicity::item_counter >
            >::type my_segmented_queue_traits;
            \endcode
            This code creates \p %SegmentedQueue type traits with item counting feature,
            all other \p type_traits members left unchanged.

            \p Options are:
            - \p opt::node_allocator - node allocator.
            - \p opt::stat - internal statistics, possible type: \ref stat, \ref empty_stat (the default)
            - \p opt::item_counter - item counting feature. Note that atomicity::empty_item_counetr is not suitable
                for segmented queue.
            - \p opt::memory_model - memory model, default is \p opt::v::relaxed_ordering.
                See option description for the full list of possible models
            - \p opt::alignment - the alignmentfor critical data, see option description for explanation
            - \p opt::allocator - the allocator used to maintain segments.
            - \p opt::lock_type - a mutual exclusion lock type used to maintain internal list of allocated
                segments. Default is \p cds::opt::Spin, \p std::mutex is also suitable.
            - \p opt::permutation_generator - a random permutation generator for sequence [0, quasi_factor),
                default is cds::opt::v::random2_permutation<int>
        */
        template <CDS_DECL_OPTIONS9>
        struct make_traits {
#   ifdef CDS_DOXYGEN_INVOKED
            typedef implementation_defined type ;   ///< Metafunction result
#   else
            typedef typename cds::opt::make_options<
                typename cds::opt::find_type_traits< type_traits, CDS_OPTIONS9 >::type
                ,CDS_OPTIONS9
            >::type   type;
#   endif
        };

    } // namespace segmented_queue

    //@cond
    namespace details {

        template <typename GC, typename T, typename Traits>
        struct make_segmented_queue
        {
            typedef GC      gc;
            typedef T       value_type;
            typedef Traits  original_type_traits;

            typedef cds::details::Allocator< T, typename original_type_traits::node_allocator > cxx_node_allocator;
            struct node_disposer {
                void operator()( T * p )
                {
                    cxx_node_allocator().Delete( p );
                }
            };

            struct intrusive_type_traits: public original_type_traits {
                typedef node_disposer   disposer;
            };

            typedef cds::intrusive::SegmentedQueue< gc, value_type, intrusive_type_traits > type;
        };

    } // namespace details
    //@endcond

    /// Segmented queue
    /** @ingroup cds_nonintrusive_queue

        The queue is based on work
        - [2010] Afek, Korland, Yanovsky "Quasi-Linearizability: relaxed consistency for improved concurrency"

        In this paper the authors offer a relaxed version of linearizability, so-called quasi-linearizability,
        that preserves some of the intuition, provides a flexible way to control the level of relaxation
        and supports th implementation of more concurrent and scalable data structure.
        Intuitively, the linearizability requires each run to be equivalent in some sense to a serial run
        of the algorithm. This equivalence to some serial run imposes strong synchronization requirements
        that in many cases results in limited scalability and synchronization bottleneck.

        The general idea is that the queue maintains a linked list of segments, each segment is an array of
        nodes in the size of the quasi factor, and each node has a deleted boolean marker, which states
        if it has been dequeued. Each producer iterates over last segment in the linked list in some random
        permutation order. Whet it finds an empty cell it performs a CAS operation attempting to enqueue its
        new element. In case the entire segment has been scanned and no available cell is found (implying
        that the segment is full), then it attempts to add a new segment to the list.

        The dequeue operation is similar: the consumer iterates over the first segment in the linked list
        in some random permutation order. When it finds an item which has not yet been dequeued, it performs
        CAS on its deleted marker in order to "delete" it, if succeeded this item is considered dequeued.
        In case the entire segment was scanned and all the nodes have already been dequeued (implying that
        the segment is empty), then it attempts to remove this segment from the linked list and starts
        the same process on the next segment. If there is no next segment, the queue is considered empty.

        Based on the fact that most of the time threads do not add or remove segments, most of the work
        is done in parallel on different cells in the segments. This ensures a controlled contention
        depending on the segment size, which is quasi factor.

        The segmented queue is an <i>unfair</i> queue since it violates the strong FIFO order but no more than
        quasi factor. It means that the consumer dequeues any item from the current first segment.

        Template parameters:
        - \p GC - a garbage collector, possible types are cds::gc::HP, cds::gc::PTB
        - \p T - the type of values stored in the queue
        - \p Traits - queue type traits, default is segmented_queue::type_traits.
            segmented_queue::make_traits metafunction can be used to construct your
            type traits.
    */
    template <class GC, typename T, typename Traits = segmented_queue::type_traits >
    class SegmentedQueue:
#ifdef CDS_DOXYGEN_INVOKED
        public cds::intrusive::SegmentedQueue< GC, T, Traits >
#else
        public details::make_segmented_queue< GC, T, Traits >::type
#endif
    {
        //@cond
        typedef details::make_segmented_queue< GC, T, Traits > maker;
        typedef typename maker::type base_class;
        //@endcond
    public:
        typedef GC  gc          ;   ///< Garbage collector
        typedef T   value_type  ;   ///< type of the value stored in the queue
        typedef Traits options  ;   ///< Queue's traits

        typedef typename options::node_allocator node_allocator;   ///< Node allocator
        typedef typename base_class::memory_model  memory_model;   ///< Memory ordering. See cds::opt::memory_model option
        typedef typename base_class::item_counter  item_counter;   ///< Item counting policy, see cds::opt::item_counter option setter
        typedef typename base_class::stat          stat        ;   ///< Internal statistics policy
        typedef typename base_class::lock_type     lock_type   ;   ///< Type of mutex for maintaining an internal list of allocated segments.
        typedef typename base_class::permutation_generator permutation_generator; ///< Random permutation generator for sequence [0, quasi-factor)

        static const size_t m_nHazardPtrCount = base_class::m_nHazardPtrCount ; ///< Count of hazard pointer required for the algorithm

    protected:
        //@cond
        typedef typename maker::cxx_node_allocator  cxx_node_allocator;
        typedef std::unique_ptr< value_type, typename maker::node_disposer >  scoped_node_ptr;

        static value_type * alloc_node( value_type const& v )
        {
            return cxx_node_allocator().New( v );
        }

        static value_type * alloc_node()
        {
            return cxx_node_allocator().New();
        }

        template <typename... Args>
        static value_type * alloc_node_move( Args&&... args )
        {
            return cxx_node_allocator().MoveNew( std::forward<Args>( args )... );
        }

        struct dummy_disposer {
            void operator()( value_type * p )
            {}
        };
        //@endcond

    public:
        /// Initializes the empty queue
        SegmentedQueue(
            size_t nQuasiFactor     ///< Quasi factor. If it is not a power of 2 it is rounded up to nearest power of 2. Minimum is 2.
            )
            : base_class( nQuasiFactor )
        {}

        /// Clears the queue and deletes all internal data
        ~SegmentedQueue()
        {}

        /// Inserts a new element at last segment of the queue
        /**
            The function makes queue node in dynamic memory calling copy constructor for \p val
            and then it calls intrusive::SEgmentedQueue::enqueue.
            Returns \p true if success, \p false otherwise.
        */
        bool enqueue( value_type const& val )
        {
            scoped_node_ptr p( alloc_node(val) );
            if ( base_class::enqueue( *p ) ) {
                p.release();
                return true;
            }
            return false;
        }

        /// Synonym for <tt>enqueue(value_type const&)</tt> function
        bool push( value_type const& val )
        {
            return enqueue( val );
        }

        /// Inserts a new element at last segment of the queue using copy functor
        /**
            \p Func is a functor called to copy value \p data of type \p Q
            which may be differ from type \ref value_type stored in the queue.
            The functor's interface is:
            \code
            struct myFunctor {
                void operator()(value_type& dest, Q const& data)
                {
                    // // Code to copy \p data to \p dest
                    dest = data;
                }
            };
            \endcode
            You may use \p boost:ref construction to pass functor \p f by reference.
        */
        template <typename Q, typename Func>
        bool enqueue( Q const& data, Func f  )
        {
            scoped_node_ptr p( alloc_node() );
            unref(f)( *p, data );
            if ( base_class::enqueue( *p )) {
                p.release();
                return true;
            }
            return false;
        }

        /// Synonym for <tt>enqueue(Q const&, Func)</tt> function
        template <typename Q, typename Func>
        bool push( Q const& data, Func f )
        {
            return enqueue( data, f );
        }

        /// Enqueues data of type \ref value_type constructed with <tt>std::forward<Args>(args)...</tt>
        template <typename... Args>
        bool emplace( Args&&... args )
        {
            scoped_node_ptr p( alloc_node_move( std::forward<Args>(args)... ) );
            if ( base_class::enqueue( *p )) {
                p.release();
                return true;
            }
            return false;
        }

        /// Removes an element from first segment of the queue
        /**
            \p Func is a functor called to copy dequeued value to \p dest of type \p Q
            which may be differ from type \ref value_type stored in the queue.
            The functor's interface is:
            \code
            struct myFunctor {
                void operator()(Q& dest, value_type const& data)
                {
                    // Code to copy \p data to \p dest
                    dest = data;
                }
            };
            \endcode
            You may use \p boost:ref construction to pass functor \p f by reference.
        */
        template <typename Q, typename Func>
        bool dequeue( Q& dest, Func f )
        {
            value_type * p = base_class::dequeue();
            if ( p ) {
                unref(f)( dest, *p );
                gc::template retire< typename maker::node_disposer >( p );
                return true;
            }
            return false;
        }

        /// Synonym for <tt>dequeue( Q&, Func )</tt> function
        template <typename Q, typename Func>
        bool pop( Q& dest, Func f )
        {
            return dequeue( dest, f );
        }

        /// Dequeues a value from the queue
        /**
            If queue is not empty, the function returns \p true, \p dest contains copy of
            dequeued value. The assignment operator for type \ref value_type is invoked.
            If queue is empty, the function returns \p false, \p dest is unchanged.
        */
        bool dequeue( value_type& dest )
        {
            typedef cds::details::trivial_assign<value_type, value_type> functor;
            return dequeue( dest, functor() );
        }

        /// Synonym for <tt>dequeue(value_type&)</tt> function
        bool pop( value_type& dest )
        {
            return dequeue( dest );
        }

        /// Checks if the queue is empty
        /**
            The original segmented queue algorithm does not allow to check emptiness accurately
            because \p empty() is unlinearizable.
            This function tests queue's emptiness checking <tt>size() == 0</tt>,
            so, the item counting feature is an essential part of queue's algorithm.
        */
        bool empty() const
        {
            return base_class::empty();
        }

        /// Clear the queue
        /**
            The function repeatedly calls \ref dequeue until it returns \p nullptr.
            The disposer specified in \p Traits template argument is called for each removed item.
        */
        void clear()
        {
            base_class::clear();
        }

        /// Returns queue's item count
        size_t size() const
        {
            return base_class::size();
        }

        /// Returns reference to internal statistics
        /**
            The type of internal statistics is specified by \p Traits template argument.
        */
        const stat& statistics() const
        {
            return base_class::statistics();
        }

        /// Returns quasi factor, a power-of-two number
        size_t quasi_factor() const
        {
            return base_class::quasi_factor();
        }
    };

}} // namespace cds::container

#endif // #ifndef __CDS_CONTAINER_SEGMENTED_QUEUE_H
