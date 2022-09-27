# OS Lab 3

## Non-preemptive schedule

In this lab, we implement a **non-preemptive** kernel thread. That is to say, thread calls `_sched()` to give up CPU, instead of interupted by hardware (we have timer enabled, but we do NOT schedule during timer interuption).

Compared with preemptive schedule, this kind of schedule is more like a funcation call. That means, thread has saved all caller-saved registers before schedule (thanks, compiler!). Jobs left for our scheduler are (1) saving all callee-saved registers, (2) choosing another thread to keep going and (3) jumping to that thread.

First we implement (3), which we called **mechanism** part in this class.

## Mechanism: How to jump another thread

Remember what we do in Attack Lab (of CSAPP), we change the context of address where function caller saves the return address and it turns out when function callee returns, control is in our hands! Take it easy, we are the Benevolent Dictator instead of attacker this time. We carefully save replace the return address with another thread, so that when `_sched()` returns the new thread gets the control. Sometimes in the future, the old thread gets the control and it looks like nothing happend but just a sub-rountine call. Great job!

![](non-preemptive.excalidraw.png)

In Procedure Call Standard(PCS) of Arm architecture, callee saves the return address in `X30` register (also called link register `LR`). So we replace this register and carefully save the old value somewhere.

Next we implement (1).

## Mechanism 2: How to save registers

## Policy: Which thread to choose
