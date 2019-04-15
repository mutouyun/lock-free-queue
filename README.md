# Lock-Free Queue

[![Build Status](https://travis-ci.org/mutouyun/lock-free.svg?branch=master)](https://travis-ci.org/mutouyun/lock-free) [![Build status](https://ci.appveyor.com/api/projects/status/github/mutouyun/lock-free?branch=master&svg=true)](https://ci.appveyor.com/project/mutouyun/lock-free) [![](https://img.shields.io/badge/speech-bilibili-ff69b4.svg)](https://www.bilibili.com/video/av47644468/?p=1)

lock-free linked-queue & ring-buffer queue
* 演讲ppt：[Lock-Free Queue](lock-free.pptx)

## Reference

 * [无锁队列的实现 | 酷 壳 - CoolShell](https://coolshell.cn/articles/8239.html)
 * [Yet another implementation of a lock-free circular array queue | CodeProject](https://www.codeproject.com/Articles/153898/Yet-another-implementation-of-a-lock-free-circular)
 * [无锁数据结构（基础篇）：原子性、原子性原语](http://blog.jobbole.com/90811/)
 * [无锁数据结构（基础篇）：内存栅障](http://blog.jobbole.com/101977/)
 * [无锁数据结构（基础篇）：内存模型](http://blog.jobbole.com/102360/)
 * [无锁数据结构（机制篇）：内存管理规则](http://blog.jobbole.com/107955/)
 * [3.5 可线性化性 - 51CTO.COM](http://book.51cto.com/art/201305/396684.htm)
 * [线性一致性(Linear consistency)，串行一致性(或顺序一致性Sequential consistency)，静态一致性(Quiescent consistency) | Jianning's space](https://jnxnj.wordpress.com/2009/01/30/%E7%BA%BF%E6%80%A7%E4%B8%80%E8%87%B4%E6%80%A7linear-consistency%EF%BC%8C%E4%B8%B2%E8%A1%8C%E4%B8%80%E8%87%B4%E6%80%A7%E6%88%96%E9%A1%BA%E5%BA%8F%E4%B8%80%E8%87%B4%E6%80%A7sequential-consistency/)
 * [多线程程序开发踩坑记 | _kawaiiQ's blog](https://kawaiiq.xyz/articles/11/)
 * [Lock-Free 编程 | 匠心十年 - 博客园](http://www.cnblogs.com/gaochundong/p/lock_free_programming.html)
 * [Java并发编程 | 金融通的博客](https://rongtongjin.github.io/2017/09/15/Java%E5%B9%B6%E5%8F%91%E7%BC%96%E7%A8%8B/)
 * [Category: 并行编程 - Yebangyu's Blog](http://www.yebangyu.org/blog/categories/bing-xing-bian-cheng/)
 * [测试分布式系统的线性一致性 - 知乎](https://zhuanlan.zhihu.com/p/29101097)
 * [分布式系统中的一致性 - 知乎](https://zhuanlan.zhihu.com/p/33711664)
 * [Linearizability 和 Serializability | io.Seeker](http://www.ioseeker.com/2018/03/16/linearizability_and_serializability/)

## Papers

 * [Lock-Free Data Structures | Dr Dobb's](http://www.drdobbs.com/lock-free-data-structures/184401865)
 * [Implementing Lock-Free Queues - John D. Valois](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.53.8674&rep=rep1&type=pdf)
 * [Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms - Maged M. Michael, Michael L. Scott](http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf)
 * [On the Nature of Progress - Maurice Herlihy, Nir Shavit](http://www.cs.tau.ac.il/~shanir/progress.pdf)
 * [基于共享内存的多核时代数据结构研究 - 周维, 周可人, 栾钟治, 姚绍文, 钱德沛](http://www.jos.org.cn/ch/reader/create_pdf.aspx?file_no=5021&journal_id=jos)

## Books

 * [The Art of Multiprocessor Programming - Maurice Herlihy, Nir Shavit](http://courses.csail.mit.edu/6.852/08/papers/lists-book-chapter.pdf)
 * [C++ Concurrency in Action - Anthony Williams, 陈晓伟（译）](http://wiki.jikexueyuan.com/project/cplusplus-concurrency-action/)

## Libraries

 * [Chapter 22. Boost.Lockfree - 1.69.0](https://www.boost.org/doc/libs/1_69_0/doc/html/lockfree.html)
 * [cameron314/concurrentqueue: A fast multi-producer, multi-consumer lock-free concurrent queue for C++11](https://github.com/cameron314/concurrentqueue)
 * [LMAX-Exchange/disruptor: High Performance Inter-Thread Messaging Library](https://github.com/LMAX-Exchange/disruptor)
   * [高性能队列——Disruptor - 知乎](https://zhuanlan.zhihu.com/p/23863915)
   * [高效内存无锁队列 Disruptor | shanshanpt](http://www.okyes.me/2016/11/01/disruptor.html)
   * [剖析Disruptor:为什么会这么快？（一）Ringbuffer的特别之处 | 并发编程网 – ifeve.com](http://ifeve.com/dissecting-disruptor-whats-so-special/)
 * [MengRao/WFMPMC: A bounded wait-free(almost) zero-copy MPMC queue written in C++11, which can also reside in SHM for IPC](https://github.com/MengRao/WFMPMC)
   * [一个Wait-Free MPMC队列的实现 - 知乎](https://zhuanlan.zhihu.com/p/46826262)
