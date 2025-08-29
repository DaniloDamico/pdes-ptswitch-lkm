#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pid.h>     /* For pid types */
#include <linux/tty.h>     /* For the tty declarations */
#include <linux/version.h> /* For LINUX_VERSION_CODE */
#include <linux/uaccess.h> // nuovo include
#include <linux/ioctl.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/mutex.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0) // alcune struct di mmap cambiano in delle versioni 5.x del kernel
#error "ptswitch requires Linux kernel >= 6.0"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("danilo");
MODULE_DESCRIPTION("module for ptswitch demo");

#define MODNAME "PTSWTCH"
#define DEVICE_NAME "ptswitch"

static ssize_t dev_read(struct file *, char __user *buf, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *buf, size_t, loff_t *);
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);

static int dev_mmap(struct file *filp, struct vm_area_struct *vma);
static vm_fault_t pts_fault(struct vm_fault *vmf);

static int Major; /* Major number assigned to char-device driver */
static DEFINE_MUTEX(device_state);

#define PAGE_LEN 4096          // una pagina standard
static char pts_buf[PAGE_LEN]; // buffer interno al device
static size_t pts_len;         // quanti byte validi

static struct page *pg_v0;
static struct page *pg_v1;

static int dev_open(struct inode *inode, struct file *file)
{

   // this device file is single instance
   if (!mutex_trylock(&device_state))
   {
      return -EBUSY;
   }

   pr_info("%s: device file successfully opened by thread %d\n", MODNAME, current->pid);
   // device opened by a default nop
   return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{

   mutex_unlock(&device_state);

   pr_info("%s: device file closed by thread %d\n", MODNAME, current->pid);
   // device closed by default nop
   return 0;
}

static ssize_t dev_write(struct file *filp, const char __user *ubuf, size_t len, loff_t *ppos)
{
   size_t n = min(len, (size_t)PAGE_LEN);
   if (copy_from_user(pts_buf, ubuf, n))
      return -EFAULT;

   pts_len = n;
   pr_info("%s: write stored %zu bytes\n", MODNAME, n);
   return len; // scrivo al massimo una pagina, il resto lo butto
}

static ssize_t dev_read(struct file *filp, char __user *ubuf, size_t len, loff_t *ppos)
{

   if (*ppos >= pts_len)
      return 0; // EOF

   len = min(len, pts_len - (size_t)*ppos);
   if (copy_to_user(ubuf, pts_buf + *ppos, len))
      return -EFAULT;

   *ppos += len;
   pr_info("%s: read returned %zu bytes (pos=%lld/%zu)\n", MODNAME, len, *ppos, pts_len);
   return len;
}

#define PTS_IOCTL_NOME 'v' // nome del comando, 'versione'
#define PTS_IOCTL_SET _IOW(PTS_IOCTL_NOME, 1, int)
#define PTS_IOCTL_TOGGLE _IO(PTS_IOCTL_NOME, 2)
#define PTS_IOCTL_GET _IOR(PTS_IOCTL_NOME, 3, int)

static atomic_t pts_version = ATOMIC_INIT(0); // 0=v0, 1=v1

// ioctl syscall per comunciare con un dispositivo
static long dev_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
   int v;
   switch (cmd)
   {
   case PTS_IOCTL_SET:
      if (copy_from_user(&v, (int __user *)arg, sizeof(v)))
         return -EFAULT;

      atomic_set(&pts_version, v ? 1 : 0);
      pr_info("%s: SET -> %d\n", MODNAME, atomic_read(&pts_version));
      break;

   case PTS_IOCTL_TOGGLE:
      v = atomic_read(&pts_version);
      atomic_set(&pts_version, !v);
      pr_info("%s: TOGGLE -> %d\n", MODNAME, atomic_read(&pts_version));
      break;

   case PTS_IOCTL_GET:
      v = atomic_read(&pts_version);
      if (copy_to_user((int __user *)arg, &v, sizeof(v)))
         return -EFAULT;

      break;
   default:
      return -ENOTTY; // errore ioctl per input non valido
   }

   /* invalida mappa (unmap_mapping_range)
   per forzare un nuovo fault e rimappare la pagina giusta. */
   if (filp && filp->f_inode && filp->f_inode->i_mapping)
   {
      unmap_mapping_range(filp->f_inode->i_mapping, 0, PAGE_SIZE, 0);
   }

   return 0;
}

static void fill_page(struct page *pg, const char *text, char fill)
{
   void *v = kmap_local_page(pg);   // mappa pagina
   memset(v, fill, PAGE_SIZE);      // caratteri di default
   strncpy(v, text, PAGE_SIZE - 1); // testo effettivo

   ((char *)v)[PAGE_SIZE - 1] = '\0';

   kunmap_local(v); // invalida pagina
}

// gestore del fault per l'indirizzo virtuale
static vm_fault_t pts_fault(struct vm_fault *vmf)
{
   struct page *pg = atomic_read(&pts_version) ? pg_v1 : pg_v0;

   get_page(pg); // refcount++ prima di inserirla nella VMA
   // crea la PTE per l'address faultato nella page table del processo
   return vmf_insert_page(vmf->vma, vmf->address, pg);
}

// struct che gestisce i fault di pagina da passare quando faccio mmap
static const struct vm_operations_struct pts_vm_ops = {
    .fault = pts_fault,
};

static int dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
   unsigned long len = vma->vm_end - vma->vm_start;
   if (len != PAGE_SIZE)
      return -EINVAL; // mappo una sola pagina

   // inserisci pg_v0 e pg_v1
   vm_flags_set(vma, VM_MIXEDMAP);                 // flag per pagine personalizzate
   vm_flags_set(vma, VM_DONTDUMP | VM_DONTEXPAND); // non far toccare le pagine dal kernel

   // non inseriamo subito la pagina: al primo accesso scatterà il fault
   vma->vm_ops = &pts_vm_ops;
   return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE, // do not forget this
    .write = dev_write,
    .read = dev_read,
    .open = dev_open,
    .release = dev_release,
    .mmap = dev_mmap,
    .unlocked_ioctl = dev_unlocked_ioctl};

int __init init_module(void)
{

   Major = __register_chrdev(0, 0, 256, DEVICE_NAME, &fops);

   if (Major < 0)
   {
      pr_err("%s: registering device failed\n", MODNAME);
      return Major;
   }

   pr_info("%s: registered, it is assigned major number %d\n", MODNAME, Major);

   // alloca le due pagine
   pg_v0 = alloc_page(GFP_KERNEL);
   pg_v1 = alloc_page(GFP_KERNEL);
   if (!pg_v0 || !pg_v1)
   {
      pr_err("%s: alloc_page failed\n", MODNAME);
      if (pg_v0)
         __free_page(pg_v0);
      if (pg_v1)
         __free_page(pg_v1);
      unregister_chrdev(Major, DEVICE_NAME);
      return -ENOMEM;
   }

   // per il momento ci scrivo qualcosa per distinguerle
   fill_page(pg_v0, "[v0] hello from version 0\n", 'A');
   fill_page(pg_v1, "[v1] hello from version 1\n", 'B');

   return 0;
}

void __exit cleanup_module(void)
{

   if (pg_v0)
      __free_page(pg_v0);
   if (pg_v1)
      __free_page(pg_v1);

   unregister_chrdev(Major, DEVICE_NAME);

   pr_info("%s: unregistered, it was assigned major number %d\n", MODNAME, Major);
}
