#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'config'))
import usb_irq_affinity as irq


class UsbIrqTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.sys = self.root/'sys'; self.proc = self.root/'proc'; self.state = self.root/'state.json'
        values = {'sys/firmware/devicetree/base/compatible': 'rockchip,rk3588\0',
                  'sys/devices/system/cpu/online': '0-7', 'proc/irq/default_smp_affinity': 'ff',
                  'proc/sys/kernel/random/boot_id': 'boot-a',
                  'proc/interrupts': ' CPU0 CPU1 CPU2 CPU3 CPU4 CPU5 CPU6 CPU7\n20: 3 0 0 0 0 0 0 0 GICv3 247 Level ehci_hcd:usb1\n164: 0 0 3 0 0 0 0 0 ITS-MSI 524288 Edge xhci_hcd\n165: 0 0 0 0 0 0 0 0 ITS-MSI 524289 Edge xhci_hcd\n175: 4 0 0 0 0 0 0 0 ITS-MSI 123 Edge wifi\n'}
        values.update({f'sys/devices/system/cpu/cpu{c}/cpu_capacity':str(414 if c<4 else 1024) for c in range(8)})
        values.update({f'proc/irq/{n}/smp_affinity_list':'0-7' for n in (20,164,165,175)})
        for name,value in values.items():
            p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(value)

    def mask(self,n):return self.proc/f'irq/{n}/smp_affinity_list'

    def test_default_usb_only_and_restore(self):
        irq.apply(self.sys,self.proc,self.state)
        self.assertEqual(self.mask(20).read_text().strip(),'4');self.assertEqual(self.mask(164).read_text().strip(),'5')
        self.assertEqual(self.mask(165).read_text(),'0-7');self.assertEqual(self.mask(175).read_text(),'0-7')
        irq.restore(self.proc,self.state)
        self.assertEqual(self.mask(20).read_text().strip(),'0-7');self.assertFalse(self.state.exists())

    def test_firmware_big_and_explicit_policy_preserved(self):
        self.mask(20).write_text('6-7');self.mask(164).write_text('2')
        irq.apply(self.sys,self.proc,self.state)
        self.assertEqual(self.mask(20).read_text(),'6-7');self.assertEqual(self.mask(164).read_text(),'2');self.assertFalse(self.state.exists())

    def test_balance_conflict_does_not_change_any_irq(self):
        with self.assertRaisesRegex(RuntimeError,'irqbalance'):irq.apply(self.sys,self.proc,self.state,True)
        self.assertEqual(self.mask(20).read_text(),'0-7');self.assertFalse(self.state.exists())

    def test_external_change_survives_restart_and_stop(self):
        irq.apply(self.sys,self.proc,self.state);self.mask(20).write_text('7')
        irq.apply(self.sys,self.proc,self.state);irq.restore(self.proc,self.state)
        self.assertEqual(self.mask(20).read_text(),'7');self.assertEqual(self.mask(164).read_text().strip(),'0-7')

    def test_reused_irq_or_different_boot_not_restored(self):
        irq.apply(self.sys,self.proc,self.state)
        interrupts=self.proc/'interrupts';interrupts.write_text(interrupts.read_text().replace('ehci_hcd:usb1','ehci_hcd:usb2'))
        irq.restore(self.proc,self.state);self.assertEqual(self.mask(20).read_text().strip(),'4')
        irq.apply(self.sys,self.proc,self.state)
        (self.proc/'sys/kernel/random/boot_id').write_text('boot-b')
        irq.restore(self.proc,self.state);self.assertEqual(self.mask(164).read_text().strip(),'5')

    def test_controller_bus_order_matches_supplier_spread(self):
        devices = self.sys/'bus/usb/devices';devices.mkdir(parents=True)
        for name in ('usb1','usb2','usb3','usb4'):(devices/name).mkdir()
        rows={20:'GICv3 247 Level ehci_hcd:usb1',22:'GICv3 250 Level ehci_hcd:usb2',
              21:'GICv3 248 Level ohci_hcd:usb3',23:'GICv3 251 Level ohci_hcd:usb4'}
        self.assertEqual(irq.ordered_usb_irqs(self.sys, rows),[20,22,21,23])

    def test_other_soc_skipped(self):
        (self.sys/'firmware/devicetree/base/compatible').write_text('other')
        irq.apply(self.sys,self.proc,self.state);self.assertFalse(self.state.exists())


if __name__=='__main__':unittest.main()
