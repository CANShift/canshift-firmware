pub const BUS_SCAN_ID_CAPACITY: usize = 128;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct SeenId {
    pub id: u32,
    pub count: u32,
}

pub fn record_id(ids: &mut [SeenId], used: &mut u16, truncated: &mut bool, id: u32) {
    let len = usize::from(*used).min(ids.len());
    if let Some(seen) = ids[..len].iter_mut().find(|seen| seen.id == id) {
        seen.count = seen.count.saturating_add(1);
        return;
    }
    let Some(slot) = ids.get_mut(len) else {
        *truncated = true;
        return;
    };
    *slot = SeenId { id, count: 1 };
    *used += 1;
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Table {
        ids: [SeenId; 3],
        used: u16,
        truncated: bool,
    }

    impl Table {
        fn new() -> Self {
            Self {
                ids: [SeenId::default(); 3],
                used: 0,
                truncated: false,
            }
        }

        fn record(&mut self, id: u32) {
            record_id(&mut self.ids, &mut self.used, &mut self.truncated, id);
        }
    }

    #[test]
    fn a_repeated_id_is_counted_once_per_frame() {
        let mut table = Table::new();
        [0x370, 0x371, 0x370, 0x370]
            .into_iter()
            .for_each(|id| table.record(id));
        assert_eq!(table.used, 2);
        assert_eq!(
            table.ids[0],
            SeenId {
                id: 0x370,
                count: 3
            }
        );
        assert_eq!(
            table.ids[1],
            SeenId {
                id: 0x371,
                count: 1
            }
        );
        assert!(!table.truncated);
    }

    #[test]
    fn a_full_table_flags_truncation_and_keeps_counting_known_ids() {
        let mut table = Table::new();
        [1, 2, 3, 4, 1].into_iter().for_each(|id| table.record(id));
        assert_eq!(table.used, 3);
        assert!(table.truncated);
        assert_eq!(table.ids[0].count, 2);
    }

    #[test]
    fn id_zero_is_a_real_id() {
        let mut table = Table::new();
        table.record(0);
        table.record(0);
        assert_eq!(table.used, 1);
        assert_eq!(table.ids[0].count, 2);
    }
}
