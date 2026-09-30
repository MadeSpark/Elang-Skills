namespace QAQ.ETool.Core.Text;

/// <summary>保持插入顺序的整数集合（.支持库 行按首次使用顺序输出）</summary>
public sealed class OrderedIntSet : ISet<int>
{
    private readonly List<int> _list = new();
    private readonly HashSet<int> _set = new();
    public int Count => _list.Count;
    public bool IsReadOnly => false;
    /// <summary>首次加入时触发</summary>
    public Action<int>? OnAdd { get; set; }
    /// <summary>每次 Add 都触发（含重复）</summary>
    public Action<int>? OnTouch { get; set; }
    public bool Add(int item)
    {
        OnTouch?.Invoke(item);
        if (!_set.Add(item)) return false;
        _list.Add(item);
        OnAdd?.Invoke(item);
        return true;
    }
    void ICollection<int>.Add(int item) => Add(item);
    public void Clear() { _list.Clear(); _set.Clear(); }
    public bool Contains(int item) => _set.Contains(item);
    public void CopyTo(int[] array, int arrayIndex) => _list.CopyTo(array, arrayIndex);
    public bool Remove(int item) { if (!_set.Remove(item)) return false; _list.Remove(item); return true; }
    public IEnumerator<int> GetEnumerator() => _list.GetEnumerator();
    System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => _list.GetEnumerator();
    public void UnionWith(IEnumerable<int> other) { foreach (var x in other) Add(x); }
    public void ExceptWith(IEnumerable<int> other) { foreach (var x in other) Remove(x); }
    public void IntersectWith(IEnumerable<int> other) => throw new NotSupportedException();
    public void SymmetricExceptWith(IEnumerable<int> other) => throw new NotSupportedException();
    public bool IsProperSubsetOf(IEnumerable<int> other) => _set.IsProperSubsetOf(other);
    public bool IsProperSupersetOf(IEnumerable<int> other) => _set.IsProperSupersetOf(other);
    public bool IsSubsetOf(IEnumerable<int> other) => _set.IsSubsetOf(other);
    public bool IsSupersetOf(IEnumerable<int> other) => _set.IsSupersetOf(other);
    public bool Overlaps(IEnumerable<int> other) => _set.Overlaps(other);
    public bool SetEquals(IEnumerable<int> other) => _set.SetEquals(other);
}
