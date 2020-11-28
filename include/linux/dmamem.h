
#ifndef _DMAMEM_H_
#define _DMAMEM_H_

/*
 * Init dmamem driver
 */
void dmamem_init(void);

/*
 * Allocate pages from dmamem region
 */
int dmamem_alloc(size_t size, dma_addr_t *dma_handle, void **ret);

/*
 * Free pages previously allocated with dmamem_xxx()
 */
int dmamem_free(void *vaddr);

/*
 * Get 'fb' area reserved in dmamem
 */
#ifdef CONFIG_DMAMEM
int dmamem_fb_get(dma_addr_t *base, unsigned long *size);
#else
static inline int dmamem_fb_get(dma_addr_t *base, unsigned long *size) {
       return -ENODEV;
}
#endif

#endif /* _DMAMEM_H_ */
